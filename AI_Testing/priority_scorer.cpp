#include <string>
#include <vector>
#include <unordered_map>
#include "priority.h"

using namespace std;

unordered_map<string, int> urgencyWeights = {
    {"dying", 5}, {"unconscious", 5}, {"bleeding", 5}, {"collapsed", 5},
    {"immediately", 5}, {"urgent", 5}, {"urgently", 5}, {"emergency", 5}, {"critical", 5}, {"severe", 5},
    {"help", 1}, {"please", 1}, {"need", 1}, {"soon", 1}, {"quickly", 1}, 
    {"trapped", 5}, {"drowning", 5}, {"choking", 5}, {"seizure", 5}, {"unresponsive", 5},
    {"stroke", 5}, {"infant", 5}, {"newborn", 5}, {"paralyzed", 5}, {"amputee", 5},
    {"hemophiliac", 5}, {"suffocating", 5}, {"crushed", 5}, {"burning", 5},
    {"sos", 5}, {"asap", 5}, {"mayday", 5}, {"desperate", 3}, {"desperately", 3},
    {"struggling", 3}, {"trapped", 3}, {"astma", 3}, {"diabetic", 3}, {"pregnant", 3},
    {"heatstroke", 3}, {"smoke", 3}, {"gas", 3}, {"looters", 3}, {"threatening", 3},
    {"req", 1}, {"required", 1}, {"require", 1}, {"requires", 1}, {"pls", 1},
    {"plz", 1}, {"bring", 1}, {"send", 1}, {"deliver", 1}, {"stuck", 1},
    {"waiting", 1}, {"check", 1}, {"access", 1}, {"located", 1}, {"location", 1}
};

unordered_map<string, int> categoryBaseline = {
    {"MEDICAL", 3}, {"FIRE", 3}, {"RESCUE", 2}, {"SECURITY", 2},
    {"WATER", 1}, {"FOOD", 1}, {"SHELTER", 1}
};

string scorePriority(const vector<string>& tokens, const string& category) {
    int score = 0;

    auto baseIt = categoryBaseline.find(category);
    if (baseIt != categoryBaseline.end()) {
        score += baseIt->second;
    }

    for (const string& tok : tokens) {
        auto it = urgencyWeights.find(tok);
        if (it != urgencyWeights.end()) {
            score += it->second;
        }
    }

    if (score >= 6) return "HIGH";
    else if (score >= 3) return "MEDIUM";
    else return "LOW";
}