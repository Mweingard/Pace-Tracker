#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <string>
#include <cstdlib>
#include "httplib.h"
#include "json.hpp"  // Make sure this is the nlohmann/json.hpp you downloaded

using json = nlohmann::json;
using namespace std::chrono_literals;

struct RunSettings {
    int goalPaceSeconds = 450;         // default 7:30 min mile
    int paceRangeSeconds = 10;         // minimum 10 seconds range
    std::string youtubePlaylist = "";
    int baseVolume = 100;              // volume 1-100
    std::atomic<int> currentPaceSeconds{450};
};

RunSettings settings;
std::mutex settingsMutex;

// Parses "mm:ss" string into total seconds
int parsePaceToSeconds(const std::string& paceStr) {
    int minutes = 0, seconds = 0;
    if (sscanf(paceStr.c_str(), "%d:%d", &minutes, &seconds) == 2) {
        return minutes * 60 + seconds;
    }
    return -1;
}

// Simulate GPS pace changes (replace with real GPS integration)
void simulateGpsPace() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(settingsMutex);
            int delta = (rand() % 21) - 10; // ±10 seconds random change
            int newPace = settings.currentPaceSeconds + delta;
            if (newPace < 300) newPace = 300;     // min 5 min mile
            if (newPace > 1200) newPace = 1200;   // max 20 min mile
            settings.currentPaceSeconds = newPace;
        }
        std::this_thread::sleep_for(1s);
    }
}

int main() {
    srand((unsigned)time(nullptr));

    httplib::Server svr;

    // Start run POST endpoint to receive user settings JSON
    svr.Post("/start", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json j = json::parse(req.body);

            std::string paceStr = j.value("pace", "");
            int paceSec = parsePaceToSeconds(paceStr);
            if (paceSec <= 0) {
                res.status = 400;
                res.set_content("Invalid pace format", "text/plain");
                return;
            }

            int rangeSec = j.value("range", 10);
            if (rangeSec < 10) rangeSec = 10;

            std::string yt = j.value("youtube", "");
            int volume = j.value("volume", 100);
            if (volume < 1) volume = 1;
            if (volume > 100) volume = 100;

            {
                std::lock_guard<std::mutex> lock(settingsMutex);
                settings.goalPaceSeconds = paceSec;
                settings.paceRangeSeconds = rangeSec;
                settings.youtubePlaylist = yt;
                settings.baseVolume = volume;
                settings.currentPaceSeconds = paceSec;
            }

            res.set_content("Run started", "text/plain");
            std::cout << "Run started: pace=" << paceSec << ", range=" << rangeSec
                      << ", yt=" << yt << ", volume=" << volume << std::endl;
        }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("Invalid JSON: ") + e.what(), "text/plain");
        }
    });

    // Get current pace and adjusted volume info
    svr.Get("/pace", [](const httplib::Request&, httplib::Response& res) {
        int currentPace, goalPace, range, baseVol;
        {
            std::lock_guard<std::mutex> lock(settingsMutex);
            currentPace = settings.currentPaceSeconds;
            goalPace = settings.goalPaceSeconds;
            range = settings.paceRangeSeconds;
            baseVol = settings.baseVolume;
        }

        int diff = currentPace - goalPace;
        int adjustedVolume = baseVol;
        std::string paceStatus = "on-pace";

        if (diff > range) {
            adjustedVolume = std::max(10, baseVol - 20);
            paceStatus = "too-slow";
        } else if (diff < -range) {
            adjustedVolume = std::max(10, baseVol - 20);
            paceStatus = "too-fast";
        }

        json resp = {
            {"currentPaceSec", currentPace},
            {"goalPaceSec", goalPace},
            {"paceStatus", paceStatus},
            {"adjustedVolume", adjustedVolume}
        };

        res.set_content(resp.dump(), "application/json");
    });

    std::thread gpsThread(simulateGpsPace);
    gpsThread.detach();

    std::cout << "Server started on http://localhost:8080\n";
    svr.listen("0.0.0.0", 8080);

    return 0;
}
