#include "File.h"

#include <utility>
#include <spdlog/spdlog.h>

bool File::isComplete() {
    for (auto i: completeSegmentsBool)
        if (!i)
            return false;
    return true;
}

Segment File::getSegment(int id) {
    return segments[id];
}

int File::getSize() {
    return size;
}

SegmentId File::getSegmentIdToDownload() {
    return 0;
}

File::File(Id id, int size, std::string path) {
    this->size = size;
    this->path = path;
    this->id = id;

    generateSegments();
    numOfSegments = segments.size();
    completeSegmentsBool = std::vector<bool>(this->numOfSegments, false);
    this->dataBegin = nullptr; // todo - it should be some dataptr to file on disk
    this->dataEnd = this->dataBegin + size;
    spdlog::info("Created file with id {}", id);
    
}

void File::generateSegments() {
    if (size % DEFAULTSEGMENTSIZE == 0)
        this->numOfSegments = size / DEFAULTSEGMENTSIZE;
    else
        this->numOfSegments = size / DEFAULTSEGMENTSIZE + 1;

    for (int i = 0; i < numOfSegments; i++) {
        this->segments.emplace_back(Segment(i, this->dataBegin + DEFAULTSEGMENTSIZE * i));
        //todo
    
    spdlog::info("Generated {} segments for file with id {}", numOfSegments, id);
}

std::vector<PeerInfo> File::getPeers() {
    return peers;
}

Id File::getId() {
    return id;
}

int File::getNumOfSegments() {
    return numOfSegments;
}

std::string File::getPath() {
    return path;
}
