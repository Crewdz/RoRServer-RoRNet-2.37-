/*
This file is part of "Rigs of Rods Server" (Relay mode)
Copyright 2007 Pierre-Michel Ricordel
Contact: pricorde@rigsofrods.com
"Rigs of Rods Server" is distributed under the terms of the GNU General Public License.

"Rigs of Rods Server" is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 3 of the License.

"Rigs of Rods Server" is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "listener.h"
#include "rornet.h"
#include "messaging.h"
#include "sequencer.h"
#include "SocketW.h"
#include "logger.h"
#include "config.h"
#include "utils.h"

#include <stdexcept>
#include <string>
#include <sstream>
#include <stdio.h>

#ifdef __GNUC__
#include <stdlib.h>
#endif

void *s_lsthreadstart(void* vid)
{
    STACKLOG;
	((Listener*)vid)->threadstart();
	return NULL;
}


Listener::Listener(int port): lport( port )
{
    STACKLOG;
	running = true;
	//start a listener thread
	pthread_create(&thread, NULL, s_lsthreadstart, this);
}

Listener::~Listener(void)
{
    STACKLOG;
	//pthread_join( thread, NULL );
	running = false;
	listSocket.set_timeout(1,0);
	pthread_cancel(thread);
}

void Listener::threadstart()
{
    STACKLOG;
	Logger::log(LOG_DEBUG,"Listerer thread starting");
	//here we start
	SWBaseSocket::SWBaseError error;

	//manage the listening socket
	listSocket.bind(lport, &error);
	if (error!=SWBaseSocket::ok)
	{
		//this is an error!
		Logger::log(LOG_ERROR,"FATAL Listerer: %s", error.get_error().c_str());
		//there is nothing we can do here
		return;
		// exit(1);
	}
	listSocket.listen();

	//await connections
	Logger::log(LOG_VERBOSE,"Listener ready");
	while (running)
	{
		Logger::log(LOG_VERBOSE,"Listener awaiting connections");
		SWInetSocket *ts=(SWInetSocket *)listSocket.accept(&error);

		if (error!=SWBaseSocket::ok)
		{
			Logger::log(LOG_ERROR,"ERROR Listener: %s", error.get_error().c_str());
			if( error == SWBaseSocket::notConnected)
				break;
			else
				continue;
		}


		Logger::log(LOG_VERBOSE,"Listener got a new connection");
#ifndef NOTIMEOUT
		ts->set_timeout(600, 0);
#endif

		//receive a magic
		int type;
		int source;
		unsigned int len;
		unsigned int streamid;
		char buffer[4096];

		try
		{
			// this is the start of it all, it all starts with a simple hello
			if (Messaging::receivemessage(ts, &type, &source, &streamid, &len, buffer, 256))
				throw std::runtime_error("ERROR Listener: receiving first message");

			// make sure our first message is a hello message
			if (type != MSG2_HELLO)
			{
				Messaging::sendmessage(ts, MSG2_WRONG_VER, 0, 0, 0, 0);
				throw std::runtime_error("ERROR Listener: protocol error");
			}

			// check client version
			if(source == 5000 && (std::string(buffer) == "MasterServer"))
			{
				Logger::log(LOG_VERBOSE, "Master Server knocked ...");
				// send back some information, then close socket
				char tmp[2048]="";
				sprintf(tmp,"protocol:%s\nrev:%s\nbuild_on:%s_%s\n", RORNET_VERSION, VERSION, __DATE__, __TIME__);
				if (Messaging::sendmessage(ts, MSG2_MASTERINFO, 0, 0, (unsigned int) strlen(tmp), tmp))
				{
					throw std::runtime_error("ERROR Listener: sending master info");
				}
				// close socket
				ts->disconnect(&error);
				delete ts;
				continue;
			}

			// compare the versions if they are compatible
			if(strncmp(buffer, RORNET_VERSION, strlen(RORNET_VERSION)))
			{
				// not compatible
				Messaging::sendmessage(ts, MSG2_WRONG_VER, 0, 0, 0, 0);
				throw std::runtime_error("ERROR Listener: bad version: "+std::string(buffer)+". rejecting ...");
			}

			// compatible version, continue to send server settings
			std::string motd_str;
			{
				std::vector<std::string> lines;
				int res = Sequencer::readFile(Config::getMOTDFile(), lines);
				if(!res)
					for(std::vector<std::string>::iterator it=lines.begin(); it!=lines.end(); it++)
						motd_str += *it + "\n";
			}

			Logger::log(LOG_DEBUG,"Listener sending server settings");
			server_info_t settings;
			memset(&settings, 0, sizeof(server_info_t));
			settings.password = !Config::getPublicPassword().empty();
			strncpy(settings.info, motd_str.c_str(), motd_str.size());
			strncpy(settings.protocolversion, RORNET_VERSION, strlen(RORNET_VERSION));
			strncpy(settings.servername, Config::getServerName().c_str(), Config::getServerName().size());
			strncpy(settings.terrain, Config::getTerrainName().c_str(), Config::getTerrainName().size());

			if (Messaging::sendmessage(ts, MSG2_HELLO, 0, 0, (unsigned int) sizeof(server_info_t), (char*)&settings))
				throw std::runtime_error("ERROR Listener: sending version");

			//receive user infos
			if (Messaging::receivemessage(ts, &type, &source, &streamid, &len, buffer, 256))
			{
				std::stringstream error_msg;
				error_msg << "ERROR Listener: receiving user infos\n"
					<< "ERROR Listener: got that: "
					<< type;
				throw std::runtime_error(error_msg.str());
			}

			if (type != MSG2_USER_INFO)
				throw std::runtime_error("Warning Listener: no user name");

			if (len > sizeof(user_info_t))
				throw std::runtime_error( "Error: did not receive proper user credentials" );
			Logger::log(LOG_INFO,"Listener creating a new client...");

			user_info_t *user = (user_info_t *)buffer;
			user->authstatus = AUTH_NONE;

			// convert username UTF8->wchar (MB TO WC)
			UTFString nickname = tryConvertUTF(user->username);
			
			// authenticate
			int authflags = Sequencer::authNick(std::string(user->usertoken), nickname);

			// now copy the resulting nickname over, server enforced
			// and back (WC TO MB)
			const char *newNick = nickname.asUTF8_c_str();
			strncpy(user->username, newNick, MAX_USERNAME_LEN);

			// save the auth results
			user->authstatus = authflags;

			if( Config::isPublic() )
			{
				Logger::log(LOG_DEBUG,"password login: %s == %s?",
						Config::getPublicPassword().c_str(),
						user->serverpassword);
				if(strncmp(Config::getPublicPassword().c_str(), user->serverpassword, 40))
				{
					Messaging::sendmessage(ts, MSG2_WRONG_PW, 0, 0, 0, 0);
					throw std::runtime_error( "ERROR Listener: wrong password" );
				}

				Logger::log(LOG_DEBUG,"user used the correct password, "
						"creating client!");
			} else {
				Logger::log(LOG_DEBUG,"no password protection, creating client");
			}

			//create a new client
			Sequencer::createClient(ts, *user); // copy the user info, since the buffer will be cleared soon
			Logger::log(LOG_DEBUG,"listener returned!");
		}
		catch(std::runtime_error e)
		{
			Logger::log(LOG_ERROR, e.what());
			ts->disconnect(&error);
			delete ts;
		}
	}
}
