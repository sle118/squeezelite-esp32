#pragma once

#define MAX_VOLUME 65536

// variable weakly set in ZeroconfAuthentificator.cpp
extern char deviceId[];

namespace cspot {
// Hardcoded information sent to spotify servers
const char* const informationString = "cspot-player";
const char* const brandName = "cspot";
const char* const versionString = "cspot-1.1";
const char* const protocolVersion = "2.7.1";
const char* const defaultDeviceName = "CSpot";
const char* const swVersion = "1.0.0";

}  // namespace cspot