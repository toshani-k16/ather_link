#pragma once
#include <string>
#include <vector>
using namespace std;

string extractNeed(const vector<string>& tokens, const string& category);
string extractLocation(const string& lowercasedOriginal);
string toLowerOnly(const string& input);