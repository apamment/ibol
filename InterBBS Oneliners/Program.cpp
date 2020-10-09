#include <core/datetime.h>
#include <core/log.h>
#include <sdk/config.h>
#include <sdk/net/networks.h>
#include <sdk/msgapi/message_api_wwiv.h>
#include <sdk/msgapi/msgapi.h>
#include <OpenDoor.h>
#include "Program.h"
#include "INIReader.h"
#ifdef _MSC_VER
#include <direct.h>
#endif

static wwiv::sdk::subboard_t default_sub(const std::string& fn) {
	wwiv::sdk::subboard_t sub{};
	sub.storage_type = 2;
	sub.filename = fn;
	return sub;
}

static std::optional<wwiv::sdk::subboard_t> find_sub(const wwiv::sdk::Subs& subs, const std::string& filename) {
	for (const auto& x : subs.subs()) {
		if (_stricmp(filename.c_str(), x.filename.c_str()) == 0) {
			return { x };
		}
	}
	return std::nullopt;
}

static std::string strip_annoying_stuff(std::string str) {
	std::stringstream ss;

	ss.str("");

	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] == '\r') {
			continue;
		}
		if (str[i] == '|') {
			i += 2;
			continue;
		}
		ss << str[i];
	}

	return ss.str();
}

static void send_last_x_lines_of_file(const char* fname, size_t x) {
	std::vector<std::string> lines;

	char buffer[256];

	FILE *fptr = fopen(fname, "r");

	fgets(buffer, 256, fptr);
	while (!feof(fptr)) {
		if (buffer[strlen(buffer) - 1] == '\n') {
			buffer[strlen(buffer) - 1] = '\0';
		}
		if (buffer[strlen(buffer) - 1] == '\r') {
			buffer[strlen(buffer) - 1] = '\0';
		}
		lines.push_back(std::string(buffer));
		fgets(buffer, 256, fptr);
	}
	fclose(fptr);

	size_t start = 0;

	if (lines.size() > x) {
		start = lines.size() - x;
	}

	for (size_t i = start; i < lines.size(); i++) {
		od_disp_emu(lines.at(i).c_str(), true);
		od_printf("\r\n");
	}

	return;
}

static std::vector<std::string> word_wrap(std::string str, int len) {
	std::vector<std::string> strvec;
	size_t line_start = 0;
	size_t last_space = 0;

	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] == ' ') {
			last_space = i;
		}
		if (i > 0 && i % len == 0) {
			strvec.push_back(str.substr(line_start, last_space - line_start));
			line_start = last_space + 1;
		}
	}

	if (last_space < str.size()) {
		strvec.push_back(str.substr(line_start));
	}
	return strvec;
}

Program::Program() {
	oneliners = new std::vector<struct oneliner_t*>();
}

int Program::run() {
	INIReader inir("ibol.ini");

	if (inir.ParseError() != 0) {
		od_printf("Couldn't parse ibol.ini!\r\n");
		return -1;
	}

	wwiv_path = inir.Get("Main", "WWIV Path", "UNKNOWN");
	system_name = inir.Get("Main", "BBS Name", "A WWIV BBS");
	dat_area = inir.Get("Main", "Data Area", "UNKNOWN");

	if (dat_area == "UNKNOWN") {
		od_printf("`bright red`Data Area must be set in ibol.ini!\r\n");
		return -1;
	}

	if (wwiv_path == "UNKNOWN") {
		od_printf("`bright red`WWIV Path must be set in ibol.ini!\r\n");
		return -1;
	}
	std::filesystem::path fspath(wwiv_path);


	const auto& config = new wwiv::sdk::Config(fspath);


	if (!config->Load()) {
		od_printf("`bright red`unable to load config!\r\n");
		return -1;
	}
 
	wwiv::sdk::Networks networks(*config);


	networks.Load();

	wwiv::sdk::Subs subs(config->datadir(), networks.networks());

	if (!subs.Load()) {
		od_printf("Unable to open subs. \r\n");
		return -1;
	}
	if (!subs.exists(dat_area)) {
		od_printf("No such area: %s\r\n", dat_area.c_str());
		return -1;
	}

	const wwiv::sdk::subboard_t sub = find_sub(subs, dat_area).value_or(default_sub(dat_area));

	auto x = new wwiv::sdk::msgapi::NullLastReadImpl();
	wwiv::sdk::msgapi::MessageApiOptions opts;

	const auto& api = std::make_unique<wwiv::sdk::msgapi::WWIVMessageApi>(opts, *config, networks.networks(), x); 
	
	std::unique_ptr<wwiv::sdk::msgapi::MessageArea> area(api->CreateOrOpen(sub, -1));
	
	for (auto current = 1; current <=  area->number_of_messages(); current++) {
		auto message = area->ReadMessage(current);
		if (message->header().title() == "InterBBS Oneliner") {
			std::vector<std::string> lines;
			std::stringstream ss(message->text().text());
			std::string tmp;
			while (std::getline(ss, tmp)) {
				if (tmp[0] != 4) {
					lines.push_back(strip_annoying_stuff(tmp));
				}
			}

			struct oneliner_t* oline = new struct oneliner_t();
			oline->oneliner.str("");
			oline->author = "UNKNOWN";
			oline->source = "Some BBS";

			for (std::string line : lines) {
				if (line.substr(0, 10) == "Oneliner: ") {
					oline->oneliner << line.substr(10) << " ";
				}
				else if (line.substr(0, 8) == "Author: ") {
					oline->author = line.substr(8);
				}
				else if (line.substr(0, 8) == "Source: ") {
					oline->source = line.substr(8);
				}
			}
			oneliners->push_back(oline);
		}
	}
	
	FILE* fptr = fopen("ibol.ans", "wb");
	if (!fptr) {
		return -1;
	}

	for (size_t i = 0; i < oneliners->size(); i++) {
		std::vector<std::string> olinesplit = word_wrap(oneliners->at(i)->oneliner.str(), 52);
		fprintf(fptr, "\x1b[1;30m------------------------------------------------------------------------------\r\n");
		for (size_t x = 0; x < olinesplit.size(); x++) {
			if (x == 0) {
				fprintf(fptr, "\x1b[1;33m%25.25s\x1b[1;30m: \x1b[1;37m%-52.52s\r\n", oneliners->at(i)->author.c_str(), olinesplit.at(x).c_str());
			}
			else if (x == 1) {
				fprintf(fptr, "\x1b[1;32m%25.25s\x1b[1;30m: \x1b[1;37m%-52.52s\r\n", oneliners->at(i)->source.c_str(), olinesplit.at(x).c_str());
			}
			else {
				fprintf(fptr, "                         \x1b[1;30m: \x1b[1;37m%-52.52s\r\n", olinesplit.at(x).c_str());
			}
		}

		if (olinesplit.size() == 1) {
			fprintf(fptr, "\x1b[1;32m%25.25s\x1b[1;30m:\r\n", oneliners->at(i)->source.c_str());
		}
	}
	fprintf(fptr, "\x1b[1;30m------------------------------------------------------------------------------\r\n");
	fclose(fptr);

	while (true) {
		od_clr_scr();
		od_printf("`bright black`+----------------------------------------------------------------------------+\r\n");
		od_printf("`bright black`|`bright yellow green` InterBBS Oneliners for WWIV!                                               `bright black`|\r\n");
		od_printf("`bright black`+----------------------------------------------------------------------------+\r\n");

		send_last_x_lines_of_file("ibol.ans", 20);

		od_printf("`bright white`Do you want to (`bright green`A`bright white`)dd a Oneliner (`bright green`V`bright white`)iew all or (`bright red`Q`bright white`)uit..");

		char c = od_get_answer("AaVvQq\r");

		switch (c) {
		case 'A':
		case 'a':
		{
			std::vector<std::string> lines = add_oneliner();

			if (lines.size() > 0) {
				std::stringstream ss;
				ss.str("");
				ss << "Author: " << od_control_get()->user_handle << "\r\n";
				ss << "Source: " << system_name << "\r\n";
				for (size_t i = 0; i < lines.size(); i++) {
					ss << "Oneliner: " << lines.at(i) << "\r\n";
				}

				std::unique_ptr<wwiv::sdk::msgapi::Message> msg(area->CreateMessage());
				auto daten = wwiv::core::DateTime::now();
				msg->header().set_from_system(0);
				msg->header().set_from_usernum(od_control_get()->user_num);
				msg->header().set_title("InterBBS Oneliner");
				msg->header().set_from(std::string(od_control_get()->user_handle));
				msg->header().set_to(std::string("IBBS1LINE"));
				msg->header().set_daten(daten.to_daten_t());
				msg->text().set_text(ss.str());

				wwiv::sdk::msgapi::MessageAreaOptions area_options{};
				area_options.send_post_to_network = true;
				std::filesystem::path cpath = std::filesystem::current_path();
				chdir(wwiv_path.c_str());
				area->AddMessage(*msg, area_options);
				chdir(cpath.u8string().c_str());
			}
			return 0;
		}
		case 'v':
		case 'V':
			od_clr_scr();
			od_printf("`bright black`+----------------------------------------------------------------------------+\r\n");
			od_printf("`bright black`|`bright yellow green` InterBBS Oneliners for WWIV!                                               `bright black`|\r\n");
			od_printf("`bright black`+----------------------------------------------------------------------------+\r\n");
			od_send_file("ibol.ans");
			od_printf("`bright white`Press any key to continue...");
			od_get_key(TRUE);
			od_printf("\r\n");
			break;
		default:
			return 0;
		}
	}
	return 0;
}

std::vector<std::string> Program::add_oneliner() {
	char buffer[53];
	std::vector<std::string> lines;
	od_printf("\r\n\r\nPlease enter your oneliner (Blank line ends)....\r\n");
	while (true) {
		od_printf("`bright black`: `white`");
		od_input_str(buffer, 52, 30, 127);
		if (strlen(buffer) == 0) {
			break;
		}

		lines.push_back(std::string(buffer));
	}

	return lines;

}