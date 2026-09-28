#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include "tokenizer.h"
#include "priority.h"
#include "entities.h"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

struct NaiveBayesModel {
    vector<string> vocab;
    unordered_map<string, long long> classDocCounts;
    unordered_map<string, long long> classTotals;
    long long totalDocs;
    unordered_map<string, unordered_map<string, long long>> classWordCounts;

    static NaiveBayesModel loadFromFile(const string& path) {
        ifstream f(path);
        json j; f >> j;

        NaiveBayesModel m;
        m.vocab = j["vocab"].get<vector<string>>();
        m.totalDocs = j["total_docs"].get<long long>();

        for (auto& [label, count] : j["class_doc_counts"].items())
            m.classDocCounts[label] = count.get<long long>();

        for (auto& [label, count] : j["class_totals"].items())
            m.classTotals[label] = count.get<long long>();

        for (auto& [label, wordMap] : j["class_word_counts"].items())
            for (auto& [word, count] : wordMap.items())
                m.classWordCounts[label][word] = count.get<long long>();

        return m;
    }

    string predict(const vector<string>& tokens) {
        double bestScore = -1e18;
        string bestLabel = "UNKNOWN";
        long long vocabSize = (long long)vocab.size();

        for (auto& [label, docCount] : classDocCounts) {
            double score = log((double)docCount / (double)totalDocs);
            long long total = classTotals[label];

            for (const string& word : tokens) {
                long long wc = 0;
                auto catIt = classWordCounts.find(label);
                if (catIt != classWordCounts.end()) {
                    auto wIt = catIt->second.find(word);
                    if (wIt != catIt->second.end()) wc = wIt->second;
                }
                double prob = (double)(wc + 1) / (double)(total + vocabSize);
                score += log(prob);
            }

            if (score > bestScore) {
                bestScore = score;
                bestLabel = label;
            }
        }
        return bestLabel;
    }
};

json processMessage(const string& rawText, NaiveBayesModel& model) {
    vector<string> tokens = tokenizer(rawText);
    string category = model.predict(tokens);
    string priority = scorePriority(tokens, category);
    string need = extractNeed(tokens, category);
    string location = extractLocation(toLowerOnly(rawText));

    json result;
    result["category"] = category;
    result["need"] = need;
    result["priority"] = priority;
    result["location"] = location;
    return result;
}

int main() {
    NaiveBayesModel model = NaiveBayesModel::loadFromFile("model.json");

    string input;
    cout << "Enter disaster message: " << endl;
    getline(cin, input);

    json output = processMessage(input, model);
    cout << output.dump(2) << endl;

    return 0;
}