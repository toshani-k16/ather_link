#include <string>
#include <vector>
#include <cctype>
#include "tokenizer.h"

using namespace std;

vector<string> tokenizer(const string& input) {

    string cleanedInput;
    cleanedInput.reserve(input.size());

    for (char c : input) {
        if (isalnum(static_cast<unsigned char>(c))) {
            cleanedInput += tolower(static_cast<unsigned char>(c));
        } else {
            cleanedInput += ' ';
        }
    }

    vector<string> tokens;
    string holder;

    for (char c : cleanedInput) {
        if (!isspace(static_cast<unsigned char>(c))) {
            holder += c;
        } else {
            if (!holder.empty()) {
                tokens.push_back(holder);
                holder.clear();
            }
        }
    }
    if (!holder.empty()) {
        tokens.push_back(holder);
    }

    return tokens;
}