namespace SMS {

// From Ferdman's SMS with rotated patterns
    template <class KeyType, class ItemType>
    struct Container {

        int32_t c_height, c_width, key_shift;
        uint64_t tag_mask; 
        uint64_t index_mask; 

        typedef std::pair<KeyType, ItemType> Item; 
        typedef std::list<Item> ListType;
        typedef typename ListType::iterator Iter; 

        std::vector<ListType> items; 

        Container(int32_t aHeight, int32_t aWidth, int32_t aKeyShift, int32_t aTagBits)
            :   c_height    {aHeight},
                c_width     {aWidth},
                key_shift   {aKeyShift},
                tag_mask    {(1ULL<<aTagBits)-1},
                index_mask  {static_cast<uint64_t>(c_height)-1}
        {
            items.resize(c_height);
            for(int i = 0; i < c_height; ++i) {
                items[i].resize(c_width);
            }
        }

        ~Container() {}

        static inline uint64_t inthash(uint64_t key) {
            key += (key << 12);
            key ^= (key >> 22);
            key += (key << 4);
            key ^= (key >> 9);
            key += (key << 10);
            key ^= (key >> 2);
            key += (key << 7);
            key ^= (key >> 12);
            return key;
        }

        uint64_t index(KeyType key) {
            return (inthash(key >> key_shift) & index_mask);
        }

        uint64_t tag(KeyType key) {
            return (inthash(key >> key_shift) & tag_mask);
        }

        Iter end() {
            return items[0].end();
        }
        
        Item insert(KeyType key, ItemType item) {
            int key_index(index(key));
            Item old_item(*items[key_index].rbegin()); // Gets the last item from the set ~ uses a move to front queue for LRU
            items[key_index].pop_back();
            KeyType key_tag = tag(key);
            items[key_index].push_front(std::make_pair(key_tag, item));
            // Removed Eviction detector logic

            return old_item;
        }

        Iter find(KeyType key) {
            ListType& list(items[index(key)]);
            KeyType key_tag(tag(key));

            for(Iter i = list.begin(); i!=list.end(); ++i) {
                if(i->first == key_tag) {
                    list.push_front(*i);
                    list.erase(i);
                    return list.begin();
                }
            }
            return end();
        }

        bool erase(KeyType key) {
            ListType& list(items[index(key)]);
            KeyType key_tag(tag(key));

            for(Iter i = list.begin(); i != list.end(); ++i) {
                if(i->first == key_tag) {
                    list.erase(i);
                    list.resize(c_width);
                    return true;
                }
            }
            return false;
        }
 
    };
}
