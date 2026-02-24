#include <map>
#include <unordered_map>
#include <string>
#include <vector>

using Word = std::string;
using Query = std::vector<std::string>;
using URL = std::string;

struct Post {
    int docId;
    int location;
};

using PostingList = std::vector<Post>;

class InvertedIndex {

    size_t docId = 0;
    size_t globalPosition = 0;
    std::map<Word, PostingList> index;
    std::unordered_map<size_t, URL> urlStore;

public:
    void addDoc(std::vector<Word> words, URL url);
    std::vector<Post> findQuery(Query query);

};