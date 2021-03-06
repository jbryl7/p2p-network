#include <iostream>
#include <utility>
#include <pthread.h>
#include <unistd.h>
#include <sharedUtils.h>
#include <stdio.h>
#include <stdlib.h>
#include "networking/ResponderThread.h"
#include "download/DownloadManager.h"
#include "networking/CSocket.h"
#include "networking/RequestHandler.h"
#include "networking/SSocket.h"
#include "file/PeerInfo.h"
#include "database/Database.h"

#define TRACKER_PORT 59095
#define TRACKER_ADDRESS "172.28.1.1"
#define CLIENT_DEFAULT_PORT 59096
#define LISTENER_DEFAULT_TIMEOUT 5;
#define CLIENT_SEED_TEST_ADDRESS "172.28.1.2"

extern uint32_t GLOB_responder_port;

void runMenu(int port, std::string &trackerIp, std::shared_ptr<Database> db);

void * runResponderThread(void * arg) {
	intptr_t connFd = (uintptr_t) arg;
	ResponderThread res;
	res.run(connFd);
}

void connListen(int port) {
	GLOB_responder_port = port;
	std::string portS  = std::to_string(port);
	int socketFd = guard(socket(AF_INET, SOCK_STREAM, 0), "Could not create TCP listening socket");

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	socklen_t addr_bytes = sizeof(addr);

	guard(bind(socketFd, (struct sockaddr *) &addr, addr_bytes), "Could not bind socket to port " + portS);
	guard(listen(socketFd, 100), "Could not listen on port " + portS);

	// default tracker-client socket timeout
//	struct timeval tv;
//	tv.tv_sec = LISTENER_DEFAULT_TIMEOUT;
//	tv.tv_usec = 0;

	syslogger->info("Listening on port " + std::to_string(ntohs(addr.sin_port)));
	for (;;) {
		intptr_t conn_fd = guard(accept(socketFd, NULL, NULL), "Could not accept");
//		setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
		pthread_t thread_id;
		int ret = pthread_create(&thread_id, NULL, runResponderThread, (void*) conn_fd);
		if (ret != 0) {
			syslogger->error("Error from pthread: %d\n", ret); exit(1);
		}
		syslogger->debug("main: created thread to handle connection " + std::to_string(conn_fd));
	}
}

int main(int argc, char *argv[]) {
	initLogger("p2p-client");
	syslogger->info("p2p client starting");

	int clientPort = CLIENT_DEFAULT_PORT;
    std::string trackerIp;

	auto database = std::make_shared<Database>();
	int option;
	while((option = getopt(argc, argv, ":p:t:")) != -1) {
    	switch(option) {
			case 'p': {
				int potentialPort = (int) strtol(optarg, nullptr, 10);
				if(potentialPort<1024 || potentialPort>65535) {
					perror("Invalid clientPort number");
					exit(1);
				}
                clientPort = potentialPort;
				break;
			}
			case 't': {
				trackerIp = optarg;
				break;
			}
			case ':':
            	printf("option needs a value\n");
            	break;
         	case '?': //used for some unknown options
            	printf("unknown option: %c\n", optopt);
            	break;
		}
	}

	for(; optind < argc; optind++){ //when some extra arguments are passed
    	printf("Given extra arguments: %s\n", argv[optind]);
    }
    if (trackerIp.empty()) {
        syslogger->error("No tracker ip provided");
        return 1;
    }
	runMenu(clientPort, trackerIp, database);

	return 0;
}

void runMenu(int port, std::string &trackerIp, std::shared_ptr<Database> db) {
	bool end = false;

	while (!end) {
		printf("___________________________________ \n");
		printf("___ Welcome to Concrete Torrent ___ \n\n");
		printf("1. Add file to DB\n");						// add local file to DB so it can be further shared
		printf("2. Create new local file\n");				// create local file out of torrentFile, obligatory before requesting download
		printf("3. Request download\n");					// start download manager to begin downloading segments of a file
		printf("4. Listen on port\n");						// connListen on port given as -p argument
		printf("5. QUIT\n");
		printf("Choose option: ");

		int choice;
		
		for(;;) {
			std::cin >> choice;
			if (std::cin.fail()) {
				fprintf(stderr, "\nWrong input! Choose option: ");
				continue;
			}
			break;
		}
	

		switch (choice) {
			case 1: {
				Filename fileName;

				printf("Enter file name: ");
				std::cin >> fileName;
				if (!std::cin.fail()){
					db->loadFromFile(fileName);
					std::cout << "ADDED: " << fileName << "\n";
				} else {
					fprintf(stderr, "Wrong file name\n");
				}

				break;
			}
			case 2: {
				Filename fileName;

				printf("Enter torrent file name: ");
				std::cin >> fileName;
				if (!std::cin.fail()){
					FileManager fm;

					Torrent torrent(std::move(fileName));
					fm.createLocalFile(torrent);

					std::cout << "CREATED: " << fileName << "\n";
				} else {
					fprintf(stderr, "Wrong file name\n");
				}

				break;
			}
			case 3: {
				Filename torrentFileName;
				FileManager fm;

				printf("Enter requested torrent file name: ");
				std::cin >> torrentFileName;
				Torrent requestedTorrent(torrentFileName);
				if (!std::cin.fail()){
					if(db->isFileInDatabase(requestedTorrent)) {
						IpAddress trackerIpAddress(trackerIp, TRACKER_PORT);
						DownloadManager dm(db, db->getFile(requestedTorrent.hashed), fm, trackerIpAddress);
						auto dmThread = dm.start_manager();
						//dmThread.join();	// ???
					std::cout << "REQUESTED: " << torrentFileName << "\n";
					} else {
						break;
					}
				} else {
					fprintf(stderr, "Wrong file name\n");
				}

				break;
			}
			case 4: {
				connListen(port);
				break;
			}
			case 5: {
				end = true;
				printf("\nGood Bye :)\n");
				break;
			}
		}
	}
}
