#include "FileManager.h"
#include <sharedUtils.h>

FileManager::FileManager() {
}

FileManager::FileManager(std::shared_ptr<Database> database) : database(std::move(database))  {
}

FileManager::~FileManager() {
	for (OpenedFile *of : readLockedFiles) {
    	if (of != nullptr) {
      		delete of;
      	}
    }
    readLockedFiles.erase(readLockedFiles.begin(), readLockedFiles.end());
  	writeLockedFiles.erase(writeLockedFiles.begin(), writeLockedFiles.end());
}

void FileManager::storeFile(std::shared_ptr<File> file) {
	//Filename fileName = file->getName();
	Filename fileName = "dummy";

	writeLock(fileName);

	std::ofstream fileStream(fileName, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);

	if (fileStream.fail()) {
		//log error - couldn't open the file
		return;
	}

	char* fileContents = reinterpret_cast<char *>(file->getDataBegin());

	fileStream.seekp(0);
	fileStream.write(fileContents, file->getSize());

	if(!fileStream) {
		//TODO log error while storing file
	} else {
		//TODO log success
	}

	fileStream.close();
	writeUnlock(fileName);
}

void FileManager::storeSegmentToFile(const Filename fileName, const std::string path, const Id segmentId, const std::string& segmentData) {
	

	writeLock(fileName);

	//TODO? ASSUMING FILE WAS CREATED BEFORE RUNNING THIS METHOD !!!
	// if it's the first added segment (file doesn't exist locally yet) => create a file of given size
	// if (!fileExists(fileName)) {
	// 	createLocalFile(fileName, fileSize);
	// 	//files vector should be updated!!! TODO
	// }
	std::fstream fileStream(fileName, std::ios_base::binary | std::ios_base::out | std::ios_base::in); // "in" mode needed to prevent file contents deletion

	if (fileStream.fail()) {
		syslogger->error("File Manager couldn't open file '{}' at path '{}'", fileName, path);
		return;
	}

	//const char* segmentContents = reinterpret_cast<char *>(segmentData);
	const char* segmentContents = segmentData.c_str();

	fileStream.seekp(segmentId * DEFAULTSEGMENTSIZE, std::ios_base::beg);
	fileStream.write(segmentContents, DEFAULTSEGMENTSIZE);

	if(!fileStream) {
		syslogger->error("File Manager couldn't store into file '{}' at path '{}'", fileName, path);
	} else {
		syslogger->info("File Manager stored segment to file '{}' at path '{}'", fileName, path);
	}

	fileStream.close();
	writeUnlock(fileName);
}

// bool FileManager::fileExists(const Filename fileName) {
// 	for (File f : files) {
// 		if (f.getName() == fileName) {
// 			return true;
// 		}
// 	}
// 	return false;
// }

void FileManager::createLocalFile(Torrent& torrent, std::string path) {
	std::ofstream fileCreator(torrent.fileName, std::ios::binary);

	char byteValue = '\0';
	std::fill_n(std::ostreambuf_iterator<char>(fileCreator), torrent.size, byteValue);

	fileCreator.close();

	syslogger->info("File Manager created file at '{}./{}'", path, torrent.fileName);
}

// void FileManager::addFile(Id id, Filename name, int size, std::string path) {
// 	files.push_back(File(id, name, size, path));
// }

char* FileManager::getSegment(const Filename fileName, Id segmentId, const std::size_t segmentSize) {
	if (segmentSize > DEFAULTSEGMENTSIZE) {
		syslogger->error("File Manager couldn't get requested segment - size too big, segment size = {}", DEFAULTSEGMENTSIZE);
		return nullptr;
	}

	readLockMutex.lock();
	OpenedFile* of;
	for (OpenedFile *f : readLockedFiles) {
		if (f->fileName == fileName) {
			of = f;
			break;
		}
	}
	readLockMutex.unlock();

	if (!of->stream.is_open()) {
		syslogger->error("File Manager couldn't read the file '{}'", fileName);
		return nullptr;
	}

	of->stream.seekg(segmentId * DEFAULTSEGMENTSIZE);
	if (of->stream.tellg() != segmentId * DEFAULTSEGMENTSIZE) {
		syslogger->error("File Manager couldn't set reading position inside file '{}'", fileName);
		return nullptr;
	}

	char* buffer = static_cast<char*>(malloc(DEFAULTSEGMENTSIZE)); // not totally sure about that
	if (buffer == nullptr) {
		syslogger->error("File Manager couldn't create buffer for file '{}'", fileName);
		return nullptr;
	}

	of->stream.read(reinterpret_cast<char *>(buffer), segmentSize);

	if (!of->stream || (of->stream.eof() && !of->stream.fail())) {
		syslogger->error("File Manager couldn't read from file '{}'", fileName);
		return nullptr;
	}

	// probably last segment of the file - fill with zeros
	if (segmentSize < DEFAULTSEGMENTSIZE) {
		std::fill(buffer + segmentSize, buffer + DEFAULTSEGMENTSIZE, '\0');
	}

	return buffer;
}

bool FileManager::readLock(const Filename fileName){
	bool isLocked = true;

	writeLockMutex.lock();

	while(isLocked) {
		isLocked = false;

		for (auto writeLocked : writeLockedFiles) {
			if(writeLocked == fileName) {
				isLocked = true;
				break;
			}
		}
	
		if (isLocked) {
			writeLockMutex.unlock();
			{
				std::unique_lock<std::mutex> uLock(condVariableMutex);
				condVariable.wait(uLock);
			}
			writeLockMutex.lock();
		}
	}
	writeLockMutex.unlock();

	readLockMutex.lock();
	for (OpenedFile *of : readLockedFiles) {
		if (of->fileName == fileName) {
			//file already locked 
			//no need to perform another one, multiple readers allowed
			syslogger->info("File Manager didn't need to lock '{}' for reading again", fileName);
			readLockMutex.unlock();
			return true;
		}
	}

	//File is then not readLocked nor writeLocked
	OpenedFile* of = new OpenedFile;
	of->fileName = fileName;
  	of->stream.open(fileName, std::ifstream::in | std::ifstream::binary);

  	if (of->stream.fail()) {
  		readLockMutex.unlock();
  		syslogger->error("File Manager couldn't open file '{}' for reading at readLock", fileName);
  		return false;
  	}

  	readLockedFiles.push_back(of);

  	readLockMutex.unlock();
  	syslogger->info("File Manager locked file '{}' for reading", fileName);
  	return true;
  }


void FileManager::readUnlock(const Filename fileName) {
	readLockMutex.lock();
	for (auto f = readLockedFiles.begin(); f != readLockedFiles.end(); ++f) {
		if (fileName == (*f)->fileName) {
			(*f)->stream.close();
			delete *f;
			readLockedFiles.erase(f);
		}
	}
	readLockMutex.unlock();
	syslogger->info("File Manager unlocked file '{}' for reading", fileName);
}

void FileManager::writeLock(const Filename fileName) {
	writeLockMutex.lock();

	//is the file readLocked
	bool isLocked = true;
	readLockMutex.lock();
	while (isLocked) {
		isLocked = false;
		for (OpenedFile* of : readLockedFiles) {
			if (fileName == of->fileName) {
				isLocked = true;
				//the file is readLocked
				break;
			}
			isLocked = false;
		}

		if (isLocked) {
			readLockMutex.unlock();
			writeLockMutex.unlock();
			{
				std::unique_lock<std::mutex> uLock(condVariableMutex);
				condVariable.wait(uLock);
			}
			writeLockMutex.lock();
			readLockMutex.lock();
		}
	}
	readLockMutex.unlock();

	writeLockedFiles.push_back(fileName);

	writeLockMutex.unlock();
	syslogger->info("File Manager locked file '{}' for writing", fileName);
}

void FileManager::writeUnlock(const Filename fileName) {
	writeLockMutex.lock();

	for (auto f = writeLockedFiles.begin(); f != writeLockedFiles.end(); ++f) {
	    if ((*f) == fileName) {
	      writeLockedFiles.erase(f);
	      writeLockMutex.unlock();
	      {
	        std::lock_guard<std::mutex> lGuard(condVariableMutex);
	        condVariable.notify_all();
	      }
	      return;
	    }
  	}
  writeLockMutex.unlock();
  syslogger->info("File Manager unlocked file '{}' for writing", fileName);
}


