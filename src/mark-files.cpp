#include <string>
#include <filesystem>
#include <vector>
#include <tuple>
#include <regex>
#include <ctime>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/color.h>
#include <console/init.hpp>
#include <console/parser.hpp>
#include <console/utf8.hpp>
#include <console/progress-bar.hpp>
#include <files/files.hpp>
#include <fort.hpp>
#include <nlohmann/json.hpp>
#include "CrossAppMutex.hpp"

using json = nlohmann::ordered_json;

/*============================================
| Declaration
==============================================*/
// program version
const std::string PROGRAM_NAME = "mark-files";
const std::string PROGRAM_VERSION = "1.0";

/*============================================
| Function definitions
==============================================*/
// lambda function to show colored tags
auto add_tag = [](const fmt::color color, const std::string& text) {
  fmt::print(fmt::format(fmt::fg(color) | fmt::emphasis::bold, "[{}]\n", text));
};

// lambda functions to read json values
const auto get_str = [](const json::iterator& it, const std::string& key) { return it.value().at(key).get<std::string>(); };
const auto get_uint64 = [](const json::iterator& it, const std::string& key) { return it.value().at(key).get<uint64_t>(); };

// extract infos for all files
void extract_infos(const std::string& path,
                   const std::string& output,
                   bool restore=false)
{
  // retrieve all files from directory
  fmt::print(fmt::format(fmt::emphasis::bold, "{:<45}", "extract all files path from directory:"));
  const std::vector<std::filesystem::path>& files = files::get_files(path, std::regex(R"(^((?!\\\.).)*$)"));
  add_tag(fmt::color::green, "OK");

  // extract infos for all files
  json files_db;
  {
    console::ProgressBar progress_bar("extract infos for all files: ", files.size());
    for (const auto& f : files)
    {
      const struct stat& file_info = files::get_stat(f);
      files_db[utf8::to_utf8(f.string())] = {
        {"sha", files::get_hash(f)},
        {"ctime", file_info.st_ctime},
        {"mtime", file_info.st_mtime}
      };
      progress_bar.tick();
    }
  }

  std::vector<std::tuple<std::string,
                         bool, uint64_t, uint64_t,
                         bool, uint64_t, uint64_t>> to_update;
  if (restore)
  {
    // parse json file infos
    fmt::print(fmt::format(fmt::emphasis::bold, "{:<45}", "parsing json file:"));
    json saved_db;
    {
      std::ifstream file(output);
      if (file.good())
        saved_db = json::parse(file);
    }
    add_tag(fmt::color::green, "OK");

    // detect all files that have changed dates
    fmt::print(fmt::format(fmt::emphasis::bold, "{:<45}", "detect all files that have changed dates:"));
    for (auto new_f = files_db.begin(); new_f != files_db.end(); ++new_f)
    {
      // check if this file existed in the saved database
      auto& old_f = saved_db.find(new_f.key());
      if (old_f != saved_db.end())
      {
        // check if the checksum have changed
        if (get_str(old_f, "sha") == get_str(new_f, "sha"))
        {
          // checksum are identical => dates needs to be restored if changed
          bool ctime = false;
          const uint64_t old_ctime = get_uint64(old_f, "ctime");
          const uint64_t new_ctime = get_uint64(new_f, "ctime");
          if (old_ctime != new_ctime)
          {
            new_f.value()["ctime"] = old_ctime;
            ctime = true;
          }

          bool mtime = false;
          const uint64_t old_mtime = get_uint64(old_f, "mtime");
          const uint64_t new_mtime = get_uint64(new_f, "mtime");
          if (old_mtime != new_mtime)
          {
            new_f.value()["mtime"] = old_mtime;
            mtime = true;
          }

          if (ctime || mtime)
            to_update.push_back(std::make_tuple(new_f.key(), 
                                                ctime, old_ctime, new_ctime, 
                                                mtime, old_mtime, new_mtime));
        }
      }
    }
    add_tag(fmt::color::green, "OK");

    // restore dates to original values
    {
      console::ProgressBar progress_bar("restore dates to original values: ", to_update.size());
      for (const auto& file : to_update)
      {
        files::set_stat(utf8::from_utf8(std::get<0>(file)),
                        std::get<1>(file) ? std::get<2>(file) : 0,
                        0,
                        std::get<4>(file) ? std::get<5>(file) : 0);
        progress_bar.tick();
      }
    }
  }

  // write json to file
  fmt::print(fmt::format(fmt::emphasis::bold, "{:<45}", "write to json file:"));
  {
    std::ofstream file(output);
    if (!file.good())
      throw std::runtime_error(fmt::format("can't write file: \"{}\"", path));
    file << std::setw(2) << files_db << std::endl;
  }
  add_tag(fmt::color::green, "OK");

  // display table of update files
  if (!to_update.empty())
  {
    // create table stylesheet
    fort::utf8_table table;
    table.set_border_style(FT_NICE_STYLE);
    table.column(0).set_cell_text_align(fort::text_align::left);
    table.column(0).set_cell_content_text_style(fort::text_style::bold);
    for (int i = 1; i < 5; ++i)
    {
      table.column(i).set_cell_text_align(fort::text_align::center);
      table.column(i).set_cell_content_text_style(fort::text_style::bold);
    }

    // create header
    table << fort::header << "FILE" << "CTIME" << "RESTORED CTIME" << "MTIME" << "RESTORED MTIME" << fort::endr;

    // add rows
    for (const auto& f : to_update)
    {
      auto to_str = [&](const uint64_t timestamp) -> const std::string { 
        char buf[128];
        std::time_t ts = timestamp;
        struct tm* timeinfo = localtime(&ts);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
        return buf;
      };
      table << std::get<0>(f);
      table << (std::get<1>(f) ? to_str(std::get<3>(f)) : "");
      table << (std::get<1>(f) ? to_str(std::get<2>(f)) : "");
      table << (std::get<4>(f) ? to_str(std::get<6>(f)) : "");
      table << (std::get<4>(f) ? to_str(std::get<5>(f)) : "");
      table << fort::endr;
    }
    fmt::print("\n{}\n", table.to_string());
  }
}

int main(int argc, char** argv)
{
  // initialize Windows console
  console::init();

  // parse command-line arguments
  std::string path;
  std::string output;
  bool restore = false;
  console::parser parser(PROGRAM_NAME, PROGRAM_VERSION);
  parser.add("p", "path", "set the path that needs to be analyzed", path, true)
        .add("o", "output", "store all the extracted properties into a json file", output, true)
        .add("r", "restore", "restore the timestamp of all un-modified files", restore);
  if (!parser.parse(argc, argv))
  {
    parser.print_usage();
    return -1;
  }
  path = utf8::from_utf8(path);
  output = utf8::from_utf8(output);
  if (!std::filesystem::directory_entry(std::filesystem::path(path)).exists())
  {
    fmt::print("{} {}\n",
      fmt::format(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error: "),
      fmt::format("the directory: \"{}\" doesn't exists", path));
    return -1;
  }

  try
  {
    // extract infos for all files
    CrossAppMutex app_mutex("Global\\MarkFiles");
    extract_infos(path, output, restore);
  }
  catch (const std::exception& ex)
  {
    add_tag(fmt::color::red, "KO");
    fmt::print("{} {}\n", 
      fmt::format(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error: "), 
      ex.what());
    return -1;
  }

  return 0;
}