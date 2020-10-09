#pragma once

#include <string>
#include <sstream>
#include <vector>

struct oneliner_t {
	std::stringstream oneliner;
	std::string author;
	std::string source;
};

class Program
{
public:
	Program();
	int run();
private:
	std::vector<struct oneliner_t *> *oneliners;
	std::string wwiv_path;
	std::string system_name;
	std::string dat_area;
	std::vector<std::string> add_oneliner();
};

