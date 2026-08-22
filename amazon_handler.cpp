#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdlib>

int main(int argc, char * argv[]) {
    std::ios::sync_with_stdio(false); // Disable sync to speed up I/O operations;

    if (argc != 4) {
        std::cout << "Usage: amazon_handler AMAZON_CSV OUTPUT_GRAPH NUMBER_OF_EDGES" << std::endl;
        return 1;
    }

    int m = std::atoi(argv[3]);
    if (m <= 0) {
        std::cerr << "NUMBER_OF_EDGES must be positive." << std::endl;
        return 1;
    }
    int proportion = 0;

    std::ifstream input_file(argv[1]);
    if (!input_file) {
        std::cerr << "Cannot open input file: " << argv[1] << std::endl;
        return 1;
    }
    std::string line;

    int tot = 0;
    std::map<std::string, int> dictionary;

    std::vector<int> edge_from, edge_to, timestamp;

    while (input_file >> line) {
        if (double(timestamp.size()) - double(m) / 10 * proportion >= 0) {
            ++proportion;
            std::cout << "Reading dataset: " << double(timestamp.size()) / m * 100 << "%" << std::endl;
        }
        if (timestamp.size() == m) {
            break;
        }
        int word_count = 0;
        std::string node_name = "";
        for (int i = 0; i < line.length(); i++) {
            if (line[i] == ',') {
                if (word_count == 0) {
                    if (dictionary.find(node_name) == dictionary.end()) {
                        dictionary[node_name] = tot++;
                    }
                    edge_from.push_back(dictionary[node_name]);
                    node_name = "";
                }
                if (word_count == 1) {
                    if (dictionary.find(node_name) == dictionary.end()) {
                        dictionary[node_name] = tot++;
                    }
                    edge_to.push_back(dictionary[node_name]);
                    node_name = "";
                }
                if (word_count == 2) {
                    node_name = "";
                }
                word_count++;
            }
            else {
                node_name += line[i];
            }
        }
        timestamp.push_back(std::atoi(node_name.c_str()));
    }

    int min_timestamp = 2147483647;
    for (int i = 0; i < timestamp.size(); i++) {
        min_timestamp = std::min(timestamp[i], min_timestamp);
    }

    for (int i = 0; i < timestamp.size(); i++) {
        timestamp[i] -= min_timestamp;
        timestamp[i] /= 2592000; // Aggregate by months
    }

    std::ofstream output_file(argv[2]);
    if (!output_file) {
        std::cerr << "Cannot open output file: " << argv[2] << std::endl;
        return 1;
    }

    proportion = 0;
    for (int i = 0; i < timestamp.size(); i++) {
        if (double(i) - double(m) / 10 * proportion >= 0) {
            ++proportion;
            std::cout << "Writing dataset: " << double(i) / m * 100 << "%" << std::endl;
        }
        output_file << edge_from[i] << " " << edge_to[i] << " " << timestamp[i] << std::endl;
    }

    output_file.close();
    return 0;
}
