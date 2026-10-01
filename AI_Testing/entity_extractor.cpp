#include <string>
#include <vector>
#include <unordered_map>
#include <regex>
#include "entities.h"
#include <cctype>
using namespace std;

unordered_map<string, vector<string>> needDictionary = {
    {"MEDICAL", {"insulin", "inhaler", "bandage", "medicine", "epipen", "doctor"}},
    {"FOOD", {"food", "formula", "rice"}},
    {"WATER", {"water", "filter"}},
    {"SHELTER", {"tent", "blankets", "clothes", "shelter"}},
    {"RESCUE", {"rescue", "rope", "ladder"}},
    {"FIRE", {"extinguisher", "firefighters"}},
    {"SECURITY", {"police", "protection"}}
};

string extractNeed(const vector<string>& tokens, const string& category) {
    auto it = needDictionary.find(category);
    if (it == needDictionary.end()) return "unknown";

    const vector<string>& needs = it->second;
    for (const string& tok : tokens) {
        for (const string& need : needs) {
            if (tok == need) return need;
        }
    }
    return "unknown";
}

string extractLocation(const string& lowercasedOriginal) {
    smatch match;

    regex floorPattern(R"((\d+)(st|nd|rd|th)?\s*floor)");
    if (regex_search(lowercasedOriginal, match, floorPattern)) {
        return match.str();
    }

    regex buildingPattern(R"(building\s*[a-z0-9]+)");
    if (regex_search(lowercasedOriginal, match, buildingPattern)) {
        return match.str();
    }

    regex roomPattern(R"(room\s*\d+)");
    if (regex_search(lowercasedOriginal, match, roomPattern)) {
        return match.str();
    }

    return "unknown";
}

string toLowerOnly(const string& input) {
    string result = input;
    for (char& c : result) c = tolower(static_cast<unsigned char>(c));
    return result;
}