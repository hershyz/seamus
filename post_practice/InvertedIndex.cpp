#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "InvertedIndex.h"


void InvertedIndex::addDoc(std::vector<Word> words, URL url) {
    for (const auto &word : words) {
        index[word].emplace_back(Post{(int) docId, (int)globalPosition++ });
    }
    urlStore[docId] = url;
    docId++;
}

std::vector<Post> InvertedIndex::findQuery(Query query) {
    std::vector<Post> res;
    std::vector<int> docLists(query.size(), 0); 
    while (true) {
        int maxValue = index[query[0]][docLists[0]].docId;
        bool diff = false;
        for (int i = 1; i < docLists.size(); ++i) {
            if (index[query[i]][docLists[i]].docId > maxValue) {
                maxValue = index[query[i]][docLists[i]].docId;
                diff = true;
            }
            else if (index[query[i]][docLists[i]].docId != maxValue) diff = true;
        }

        if (!diff) {
            res.push_back(index[query[0]][docLists[0]]);
            for (int i = 0; i < docLists.size(); ++i) {
                docLists[i]++;
                if (docLists[i] >= index[query[i]].size()) return res; 
            }
        }
        else {
            for (int i = 0; i < docLists.size(); ++i) {
                while (index[query[i]][docLists[i]].docId < maxValue) {
                    docLists[i]++;
                    if (docLists[i] >= index[query[i]].size()) return res;
                }
            }
        }
    }
    return {};
}