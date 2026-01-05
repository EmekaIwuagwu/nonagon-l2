#include <iostream>

namespace nonagon {
    namespace storage {
        class Database {
        public:
            void init() {
                std::cout << "Storage initialized (Mock)." << std::endl;
            }
        };
    }
}
