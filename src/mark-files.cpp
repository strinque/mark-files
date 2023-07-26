#include <string>
#include <filesystem>
#include <vector>
#include <tuple>
#include <regex>
#include <map>
#include <ctime>
#include <queue>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/color.h>
#include <winpp/console.hpp>
#include <winpp/parser.hpp>
#include <winpp/progress-bar.hpp>
#include <winpp/utf8.hpp>
#include <winpp/files.hpp>
#include <winpp/system-mutex.hpp>
#include <winpp/win.hpp>
#include <fort.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

/*============================================
| Declaration
==============================================*/
// program version
const std::string PROGRAM_NAME = "mark-files";
const std::string PROGRAM_VERSION = "1.6.0";

// default length in characters to align status 
constexpr std::size_t g_status_len = 50;

// file information that will be extracted/computed
struct file_infos {
  std::string sha;
  std::uint64_t ctime = 0;
  std::uint64_t mtime = 0;
};

/*============================================
| Function definitions
==============================================*/
// lambda function to show colored tags
auto add_tag = [](const fmt::color color, const std::string& text) {
  fmt::print(fmt::format(fmt::fg(color) | fmt::emphasis::bold, "[{}]\n", text));
};

// execute a sequence of actions with tags
void exec(const std::string& str, std::function<void()> fct)
{
  fmt::print(fmt::emphasis::bold, "{:<" + std::to_string(g_status_len) + "}", str + ": ");
  try
  {
    fct();
    add_tag(fmt::color::green, "OK");
  }
  catch (const std::exception& ex)
  {
    add_tag(fmt::color::red, "KO");
    throw ex;
  }
}

// extract info for one file - thread
void extract_info(std::mutex& mutex,
                  std::queue<std::filesystem::path>& files,
                  std::map<std::string, struct file_infos>& files_infos,
                  console::progress_bar& progress_bar)
{
  while (true)
  {
    // retrieve one file from queue - protected by mutex
    std::filesystem::path file;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (files.empty())
        break;
      file = files.front();
      files.pop();
    }

    // retrieve infos for one file
    const struct stat& file_info = files::get_stat(file);
    const std::string& file_hash = files::get_hash(file);

    // update database and progress_bar - protected by mutex
    {
      std::lock_guard<std::mutex> lock(mutex);
      files_infos[file.u8string()] = {
        file_hash, 
        static_cast<uint64_t>(file_info.st_ctime), 
        static_cast<uint64_t>(file_info.st_mtime)
      };
      progress_bar.tick();
    }
  }
}

// extract infos for all files
void extract_infos(const std::filesystem::path& path,
                   const std::filesystem::path& output,
                   bool restore = false)
{
  // retrieve all files path from directory not hidden (not starting with .)
  std::vector<std::filesystem::path> all_files;
  exec("extract all files' path from directory", [&]() {
    const auto& dir_filter = [&](const std::filesystem::path& p) {
      return p.string().find("\\.") == std::string::npos;
    };
    all_files = files::get_files(path,
                                 files::infinite_depth,
                                 false,
                                 dir_filter,
                                 files::default_filter);
    });

  // extract infos for all files
  std::map<std::string, struct file_infos> files_infos;
  if (!all_files.empty())
  {
    console::progress_bar progress_bar("extract infos for all files:", all_files.size());

    // initialize a queue of files
    std::queue<std::filesystem::path> files(
      std::deque<std::filesystem::path>(all_files.begin(), all_files.end()));

    // start threads
    std::mutex mutex;
    const std::size_t max_cpu = static_cast<std::size_t>(std::thread::hardware_concurrency());
    const std::size_t nb_threads = std::min(files.size(), max_cpu);
    std::vector<std::thread> threads(nb_threads);
    for (auto& t : threads)
      t = std::thread(extract_info,
                      std::ref(mutex),
                      std::ref(files),
                      std::ref(files_infos),
                      std::ref(progress_bar));

    // wait for threads completion
    for (auto& t : threads)
      if (t.joinable())
        t.join();
  }
  if (files_infos.empty())
    throw std::runtime_error("empty directory");

  std::vector<std::tuple<std::string,
                         bool, uint64_t, uint64_t,
                         bool, uint64_t, uint64_t>> to_update;
  if (restore)
  {
    // parse json file infos
    json saved_db;
    exec("parsing json file", [&]() {
      std::ifstream file(output);
      if (file.good())
        saved_db = json::parse(file);
      });

    // detect all files that have changed dates
    exec("detect all files that have changed dates", [&]() {
      if (saved_db.contains("files") && saved_db["files"].is_array())
      {
        for (auto& i : saved_db["files"])
        {
          // check entry validity
          if ((!i.contains("name")  || !i["name"].is_string())  ||
              (!i.contains("sha")   || !i["sha"].is_string())   ||
              (!i.contains("ctime") || !i["ctime"].is_number()) ||
              (!i.contains("mtime") || !i["mtime"].is_number()))
            continue;

          // retrieve fields of this entry
          const std::string& name    = i["name"].get<std::string>();
          const std::string& old_sha = i["sha"].get<std::string>();
          const uint64_t old_ctime   = i["ctime"].get<uint64_t>();
          const uint64_t old_mtime   = i["mtime"].get<uint64_t>();

          // check if this file existed in the saved database
          auto it = files_infos.find(name);
          if (it == files_infos.end())
            continue;

          // check if the checksum have changed
          if (it->second.sha != old_sha)
            continue;

          // checksum are identical => dates needs to be restored if changed
          bool ctime = false;
          const uint64_t new_ctime = it->second.ctime;
          if (new_ctime != old_ctime)
          {
            it->second.ctime = old_ctime;
            ctime = true;
          }

          bool mtime = false;
          const uint64_t new_mtime = it->second.mtime;
          if (new_mtime != old_mtime)
          {
            it->second.mtime = old_mtime;
            mtime = true;
          }

          if (ctime || mtime)
            to_update.push_back(std::make_tuple(name, 
                                                ctime, old_ctime, new_ctime,
                                                mtime, old_mtime, new_mtime));
        }
      }
      });

    // restore dates to original values
    if (!to_update.empty())
    {
      console::progress_bar progress_bar("restore dates to original values:", to_update.size());
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
  exec("write to json file", [&]() {
    std::ofstream file(output, std::ios::binary);
    if (!file.good())
      throw std::runtime_error(fmt::format("can't write file: \"{}\"", output.filename().u8string()));

    // detect maximum length of filename
    std::size_t max_len = 0;
    for (const auto& [k, v] : files_infos)
      if (k.size() > max_len)
        max_len = k.size();

    // reconstruct json-optimized file manually
    std::string line_fmt;
    line_fmt += R"("name": "{:<)" + std::to_string(max_len) + R"(}, )";
    line_fmt += R"("sha": "{}", )";
    line_fmt += R"("ctime": {}, )";
    line_fmt += R"("mtime": {})";
    std::string content;
    content += "{\n";
    content += "  \"files\": [\n";
    for (const auto& [k, v] : files_infos)
    {
      content += "    { ";
      content += fmt::format(line_fmt,
        std::regex_replace(k, std::regex("\\\\"), "\\\\") + "\"",
        v.sha,
        v.ctime,
        v.mtime);
      content += " }";
      content += (k == files_infos.rbegin()->first) ? "" : ",";
      content += "\n";
    }
    content += "  ]\n";
    content += "}";

    // write to file
    file << content;
    });

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
    table << fort::header << "FILE" << "RESTORED CTIME" << "RESTORED MTIME" << fort::endr;

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
      table << (std::get<1>(f) ? 
        fmt::format("{} => {}", 
          to_str(std::get<3>(f)), 
          to_str(std::get<2>(f))) : 
        "");
      table << (std::get<4>(f) ? 
        fmt::format("{} => {}",
          to_str(std::get<6>(f)),
          to_str(std::get<5>(f))) :
        "");
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
  std::filesystem::path path;
  std::filesystem::path output;
  bool restore = false;
  bool interactive = false;
  console::parser parser(PROGRAM_NAME, PROGRAM_VERSION);
  parser.add("p", "path", "set the path that needs to be analyzed", path, true)
        .add("o", "output", "store all the extracted properties into a json file", output, true)
        .add("r", "restore", "restore the timestamp of all un-modified files", restore)
        .add("i", "interactive", "enable the interactive mode which asks user for questions", interactive);
  if (!parser.parse(argc, argv))
  {
    parser.print_usage();
    return -1;
  }

  int ret;
  try
  {
    // check arguments validity
    if (!std::filesystem::exists(path))
      throw std::runtime_error(fmt::format("the directory: \"{}\" doesn't exists", path.u8string()));

    // acquire system wide mutex to avoid multiples executions of mark-files in //
    fmt::print(fmt::emphasis::bold, "{}\n", "waiting for other mark-files programs to terminate...");
    win::system_mutex mtx("Global\\MarkFiles");
    std::lock_guard<win::system_mutex> lock(mtx);

    // extract infos for all files
    extract_infos(path, output, restore);
    ret = 0;
  }
  catch (const std::exception& ex)
  {
    fmt::print("{} {}\n", 
      fmt::format(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error:"), 
      ex.what());
    ret = -1;
  }

  // prompt user to terminate the program
  if (interactive)
    system("pause");

  return ret;
}