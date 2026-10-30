#include "cobs_tx.h"
#ifndef FIRMWARE_PIO_LITTLEFS_H_
#define FIRMWARE_PIO_LITTLEFS_H_

#include <vector>
#include <queue>
#include <utility>
#include <algorithm>

// Allocate your static buffers to prevent heap fragmentation
uint8_t lfs_read_buf[256];
uint8_t lfs_prog_buf[256];
uint8_t lfs_lookahead_buf[16];  // 16 bytes * 8 = 128 blocks tracked

const struct lfs_config lfs = {
    // Link your hardware glue functions
    .read = pico_lfs_read,
    .prog = pico_lfs_prog,
    .erase = pico_lfs_erase,
    .sync = pico_lfs_sync,

    // Block device configurations for typical RP2350 flash chips
    .read_size = 1,    // Read granularity can be down to 1 byte
    .prog_size = 256,  // Radio Shack (DECB & OS-9) sector size
    .block_size =
        LFS_BLOCK_SIZE,  // W25Q128 standard sector erase size (4096 bytes)
    .block_count = LFS_BLOCK_COUNT,  // Example: 512 blocks * 4KB = 2 Megabytes
    .block_cycles = 500,  // Dynamic wear-leveling threshold before eviction
    .cache_size = 256,    // Match your program size for performance
    .lookahead_size = 16,

    .read_buffer = lfs_read_buf,
    .prog_buffer = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,
};

lfs_t lfs_volume;
std::string vfs_cwd = "/";

// =========================================================================
// Native Tcl commands — use Tcl_SetResult for output.
// Signature: int cmd(ClientData, Tcl_Interp*, int argc, char* argv[])
// =========================================================================

extern "C" int ls_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  std::vector<std::string> targets;
  bool opt_a = false;
  bool opt_l = false;
  bool opt_d = false;
  bool opt_r = false;
  
  int arg_idx = 1;
  while (arg_idx < argc && argv[arg_idx][0] == '-') {
    for (int j = 1; argv[arg_idx][j] != '\0'; j++) {
      if (argv[arg_idx][j] == 'a') opt_a = true;
      else if (argv[arg_idx][j] == 'l') opt_l = true;
      else if (argv[arg_idx][j] == 'd') opt_d = true;
      else if (argv[arg_idx][j] == 'r') opt_r = true;
    }
    arg_idx++;
  }
  
  for (; arg_idx < argc; arg_idx++) {
    targets.push_back(argv[arg_idx]);
  }
  if (targets.empty()) {
    targets.push_back(".");
  }

  Tcl_ResetResult(interp);
  bool first_output = true;

  std::queue<std::string> dir_queue;

  for (size_t i = 0; i < targets.size(); i++) {
    coro_yield(gspoon::g_spoon_coro);
    const std::string& path = targets[i];
    
    struct vfs_info stat_info;
    int stat_err = vfs_stat(path.c_str(), &stat_info);
    
    if (stat_err < 0) {
      std::string msg = std::string("ls: cannot access '") + path + "': No such file or directory\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      continue;
    }
    
    if (stat_info.type == LFS_TYPE_REG || (opt_d && stat_info.type == LFS_TYPE_DIR)) {
      if (!first_output) Tcl_AppendResult(interp, "\n", (char*)NULL);
      std::string line = path;
      if (stat_info.type == LFS_TYPE_DIR) line += "/";
      if (opt_l && stat_info.type == LFS_TYPE_REG) {
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "    %ld", (long)stat_info.size);
        line += size_buf;
      }
      Tcl_AppendResult(interp, line.c_str(), (char*)NULL);
      first_output = false;
    } else if (stat_info.type == LFS_TYPE_DIR) {
      dir_queue.push(path);
    }
  }

  bool multiple_dirs = dir_queue.size() > 1 || opt_r || targets.size() > 1;

  while (!dir_queue.empty()) {
    coro_yield(gspoon::g_spoon_coro);
    std::string current_dir = dir_queue.front();
    dir_queue.pop();

    if (multiple_dirs) {
      if (!first_output) Tcl_AppendResult(interp, "\n\n", (char*)NULL);
      std::string header = current_dir + ":";
      Tcl_AppendResult(interp, header.c_str(), (char*)NULL);
      first_output = false;
    }
    
    vfs_dir_t dir;
    int err = vfs_dir_open(&dir, current_dir.c_str());
    if (err) {
      std::string msg = std::string("\nls: cannot open directory '") + current_dir + "'";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      first_output = false;
      continue;
    }
    
    struct Entry {
      std::string name;
      bool is_dir;
      long size;
    };
    
    std::vector<Entry> entries;
    struct vfs_info info;
    while (true) {
      int res = vfs_dir_read(&dir, &info);
      if (res <= 0) break;
      if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
      
      if (!opt_a && info.name[0] == '.') continue;
      
      Entry e;
      e.name = info.name;
      e.is_dir = (info.type == LFS_TYPE_DIR);
      e.size = info.size;
      entries.push_back(e);
    }
    vfs_dir_close(&dir);
    
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return strcmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    
    for (const auto& e : entries) {
      if (!first_output) Tcl_AppendResult(interp, "\n", (char*)NULL);
      std::string line = "  " + e.name;
      if (e.is_dir) {
        line += "/";
      } else if (opt_l) {
        char size_buf[32];
        snprintf(size_buf, sizeof(size_buf), "    %ld", (long)e.size);
        line += size_buf;
      }
      Tcl_AppendResult(interp, line.c_str(), (char*)NULL);
      first_output = false;

      if (opt_r && e.is_dir) {
        std::string next_path = current_dir;
        if (!next_path.empty() && next_path.back() != '/') next_path += "/";
        next_path += e.name;
        dir_queue.push(next_path);
      }
    }
    multiple_dirs = true;
  }
  
  return TCL_OK;
}

static bool IsNiceFilename(const char* filename) {
  if (!filename || *filename == '\0') return false;
  const char* p = filename;
  bool start_of_comp = true;
  while (*p) {
    if (*p == '/') {
      start_of_comp = true;
      p++;
      continue;
    }
    char c = *p;
    if (start_of_comp) {
      if (!(isalnum(c) || c == '.' || c == '_')) {
        return false;
      }
      start_of_comp = false;
    } else {
      if (!(isalnum(c) || c == '-' || c == '.' || c == '_' || c == '!' || 
            c == '@' || c == '%' || c == '^' || c == '+' || c == '~')) {
        return false;
      }
    }
    p++;
  }
  return true;
}

extern "C" int mkdir_cmd(ClientData, Tcl_Interp* interp, int argc,
                         char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: mkdir dir...", TCL_STATIC);
    return TCL_ERROR;
  }
  for (int i = 1; i < argc; i++) {
    coro_yield(gspoon::g_spoon_coro);
    if (!IsNiceFilename(argv[i])) {
      Tcl_SetResult(interp, (char*)"mkdir: filename contains characters that are not nice", TCL_STATIC);
      return TCL_ERROR;
    }
    int err = vfs_mkdir(argv[i]);
    if (err < 0) {
      Tcl_SetResult(interp, (char*)"Failed to mkdir", TCL_STATIC);
      return TCL_ERROR;
    }
  }
  return TCL_OK;
}

extern "C" int rmdir_cmd(ClientData, Tcl_Interp* interp, int argc,
                         char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: rmdir dir...", TCL_STATIC);
    return TCL_ERROR;
  }
  bool any_error = false;
  Tcl_ResetResult(interp);
  for (int i = 1; i < argc; i++) {
    coro_yield(gspoon::g_spoon_coro);
    struct vfs_info info;
    if (vfs_stat(argv[i], &info) == 0 && info.type != LFS_TYPE_DIR) {
      std::string msg = std::string("rmdir: failed to remove '") + argv[i] + "': Not a directory\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }
    int err = vfs_remove(argv[i]);
    if (err < 0) {
      std::string msg = std::string("rmdir: failed to remove '") + argv[i] + "'\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
    }
  }
  return any_error ? TCL_ERROR : TCL_OK;
}

extern "C" int echo_cmd(ClientData, Tcl_Interp* interp, int argc,
                        char* argv[]) {
  Tcl_ResetResult(interp);
  for (int i = 1; i < argc; i++) {
    if (i > 1) Tcl_AppendResult(interp, " ", (char*)NULL);
    Tcl_AppendResult(interp, argv[i], (char*)NULL);
  }
  return TCL_OK;
}

static std::string get_basename(const std::string& path) {
  size_t pos = path.find_last_of('/');
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

extern "C" int cp_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 3) {
    Tcl_SetResult(interp, (char*)"Usage: cp src... dst", TCL_STATIC);
    return TCL_ERROR;
  }
    coro_yield(gspoon::g_spoon_coro);
  
  const char* dst_arg = argv[argc - 1];
  struct vfs_info info;
  bool dest_is_dir = (vfs_stat(dst_arg, &info) == 0 && info.type == LFS_TYPE_DIR);
  
  if (!dest_is_dir && argc > 3) {
    Tcl_SetResult(interp, (char*)"cp: target is not a directory", TCL_STATIC);
    return TCL_ERROR;
  }
  
  bool any_error = false;
  Tcl_ResetResult(interp);

  for (int i = 1; i < argc - 1; i++) {
    coro_yield(gspoon::g_spoon_coro);
    std::string dst_path = dst_arg;
    if (dest_is_dir) {
      if (!dst_path.empty() && dst_path.back() != '/') dst_path += "/";
      dst_path += get_basename(argv[i]);
    }
    if (!IsNiceFilename(dst_path.c_str())) {
      Tcl_SetResult(interp, (char*)"cp: destination filename contains characters that are not nice", TCL_STATIC);
      return TCL_ERROR;
    }
    
    vfs_file_t src, dst;
    int err = vfs_file_open(&src, argv[i], LFS_O_RDONLY);
    if (err < 0) {
      std::string msg = std::string("cp: cannot open source ") + argv[i] + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }
    err = vfs_file_open(&dst, dst_path.c_str(), LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
      vfs_file_close(&src);
      std::string msg = std::string("cp: cannot open destination ") + dst_path + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }
    
    char buf[256];
    bool copy_error = false;
    while (true) {
    coro_yield(gspoon::g_spoon_coro);
      lfs_ssize_t n = vfs_file_read(&src, buf, sizeof(buf));
      if (n < 0) {
        std::string msg = std::string("cp: read error on ") + argv[i] + "\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        copy_error = true;
        break;
      }
      if (n == 0) break;
      lfs_ssize_t w = vfs_file_write(&dst, buf, n);
      if (w < 0) {
        std::string msg = std::string("cp: write error on ") + dst_path + "\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        copy_error = true;
        break;
      }
    }
    vfs_file_close(&src);
    vfs_file_close(&dst);
    if (copy_error) any_error = true;
  }
  
  return any_error ? TCL_ERROR : TCL_OK;
}

bool files_are_identical(const char* src_path, const char* dst_path, size_t size) {
  vfs_file_t f1, f2;
  if (vfs_file_open(&f1, src_path, LFS_O_RDONLY) < 0) return false;
  if (vfs_file_open(&f2, dst_path, LFS_O_RDONLY) < 0) {
    vfs_file_close(&f1);
    return false;
  }
  char buf1[256], buf2[256];
  bool identical = true;
  size_t remaining = size;
  while (remaining > 0) {
    size_t chunk = remaining > sizeof(buf1) ? sizeof(buf1) : remaining;
    if (vfs_file_read(&f1, buf1, chunk) != (lfs_ssize_t)chunk || 
        vfs_file_read(&f2, buf2, chunk) != (lfs_ssize_t)chunk) {
      identical = false;
      break;
    }
    if (memcmp(buf1, buf2, chunk) != 0) {
      identical = false;
      break;
    }
    remaining -= chunk;
  }
  vfs_file_close(&f1);
  vfs_file_close(&f2);
  return identical;
}

int rsync_tree(Tcl_Interp* interp, const std::string& initial_src, const std::string& initial_dst) {
  std::queue<std::pair<std::string, std::string>> dir_queue;
  std::queue<std::pair<std::string, std::string>> file_queue;

  struct vfs_info initial_info;
  if (vfs_stat(initial_src.c_str(), &initial_info) < 0) {
    std::string msg = "rsync: cannot stat " + initial_src + "\n";
    Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
    return -1;
  }

  if (initial_info.type == LFS_TYPE_DIR) {
    dir_queue.push({initial_src, initial_dst});
  } else {
    file_queue.push({initial_src, initial_dst});
  }

  bool any_error = false;

  // Process all directories first
  while (!dir_queue.empty()) {
    coro_yield(gspoon::g_spoon_coro);
    auto [src_path, dst_path] = dir_queue.front();
    dir_queue.pop();

    struct vfs_info dst_info;
    if (vfs_stat(dst_path.c_str(), &dst_info) < 0) {
      if (vfs_mkdir(dst_path.c_str()) < 0) {
        std::string msg = "rsync: cannot create directory " + dst_path + "\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        any_error = true;
        continue;
      }
    } else if (dst_info.type != LFS_TYPE_DIR) {
      std::string msg = "rsync: destination " + dst_path + " exists but is not a directory\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }

    vfs_dir_t dir;
    if (vfs_dir_open(&dir, src_path.c_str()) < 0) {
      std::string msg = "rsync: cannot open directory " + src_path + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }

    struct vfs_info info;
    while (vfs_dir_read(&dir, &info) > 0) {
      if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
      coro_yield(gspoon::g_spoon_coro);

      std::string next_src = src_path;
      if (!next_src.empty() && next_src.back() != '/') next_src += "/";
      next_src += info.name;

      std::string next_dst = dst_path;
      if (!next_dst.empty() && next_dst.back() != '/') next_dst += "/";
      next_dst += info.name;

      if (info.type == LFS_TYPE_DIR) {
        dir_queue.push({next_src, next_dst});
      } else {
        file_queue.push({next_src, next_dst});
      }
    }
    vfs_dir_close(&dir);
  }

  // Process all files next
  while (!file_queue.empty()) {
    coro_yield(gspoon::g_spoon_coro);
    auto [src_path, dst_path] = file_queue.front();
    file_queue.pop();

    struct vfs_info src_info;
    if (vfs_stat(src_path.c_str(), &src_info) < 0) {
      std::string msg = "rsync: cannot stat " + src_path + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }

    struct vfs_info dst_info;
    if (vfs_stat(dst_path.c_str(), &dst_info) == 0) {
      if (dst_info.type == LFS_TYPE_DIR) {
        std::string msg = "rsync: destination " + dst_path + " is a directory, but source is a file\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        any_error = true;
        continue;
      }
      if (dst_info.size == src_info.size) {
        if (files_are_identical(src_path.c_str(), dst_path.c_str(), src_info.size)) {
          continue;
        }
      }
    }

    vfs_file_t src, dst;
    if (vfs_file_open(&src, src_path.c_str(), LFS_O_RDONLY) < 0) {
      std::string msg = "rsync: cannot open source " + src_path + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }
    if (vfs_file_open(&dst, dst_path.c_str(), LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0) {
      vfs_file_close(&src);
      std::string msg = "rsync: cannot open destination " + dst_path + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }

    char buf[256];
    bool copy_error = false;
    while (true) {
      coro_yield(gspoon::g_spoon_coro);
      lfs_ssize_t n = vfs_file_read(&src, buf, sizeof(buf));
      if (n < 0) {
        std::string msg = "rsync: read error on " + src_path + "\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        copy_error = true;
        break;
      }
      if (n == 0) break;
      lfs_ssize_t w = vfs_file_write(&dst, buf, n);
      if (w < 0) {
        std::string msg = "rsync: write error on " + dst_path + "\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        copy_error = true;
        break;
      }
    }
    vfs_file_close(&src);
    vfs_file_close(&dst);
    if (copy_error) any_error = true;
  }

  return any_error ? -1 : 0;
}

extern "C" int rsync_a_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 3) {
    Tcl_SetResult(interp, (char*)"Usage: rsync-a srcDir destDir", TCL_STATIC);
    return TCL_ERROR;
  }
  
  coro_yield(gspoon::g_spoon_coro);
  Tcl_ResetResult(interp);
  coro_yield(gspoon::g_spoon_coro);
  if (rsync_tree(interp, argv[1], argv[2]) < 0) {
    return TCL_ERROR;
  }
  return TCL_OK;
}


extern "C" int mv_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 3) {
    Tcl_SetResult(interp, (char*)"Usage: mv src... dst", TCL_STATIC);
    return TCL_ERROR;
  }
  
  const char* dst_arg = argv[argc - 1];
  struct vfs_info info;
  bool dest_is_dir = (vfs_stat(dst_arg, &info) == 0 && info.type == LFS_TYPE_DIR);
  
  if (!dest_is_dir && argc > 3) {
    Tcl_SetResult(interp, (char*)"mv: target is not a directory", TCL_STATIC);
    return TCL_ERROR;
  }
  
  bool any_error = false;
  Tcl_ResetResult(interp);

  for (int i = 1; i < argc - 1; i++) {
    coro_yield(gspoon::g_spoon_coro);
    std::string dst_path = dst_arg;
    if (dest_is_dir) {
      if (!dst_path.empty() && dst_path.back() != '/') dst_path += "/";
      dst_path += get_basename(argv[i]);
    }
    if (!IsNiceFilename(dst_path.c_str())) {
      Tcl_SetResult(interp, (char*)"mv: destination filename contains characters that are not nice", TCL_STATIC);
      return TCL_ERROR;
    }
    
    int err = lfs_rename(&lfs_volume, argv[i], dst_path.c_str());
    if (err < 0) {
      std::string msg = std::string("mv: rename failed for ") + argv[i] + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
    }
  }
  
  return any_error ? TCL_ERROR : TCL_OK;
}

extern "C" int rm_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  bool recursive = false;
  int start_idx = 1;
  if (argc > 1 && strcmp(argv[1], "-r") == 0) {
    recursive = true;
    start_idx = 2;
  }

  if (argc < start_idx + 1) {
    Tcl_SetResult(interp, (char*)"Usage: rm [-r] file...", TCL_STATIC);
    return TCL_ERROR;
  }

  bool any_error = false;
  Tcl_ResetResult(interp);

  std::queue<std::string> traversal_queue;
  std::vector<std::string> files_to_remove;
  std::vector<std::string> dirs_to_remove;

  for (int i = start_idx; i < argc; i++) {
    coro_yield(gspoon::g_spoon_coro);
    struct vfs_info info;
    if (vfs_stat(argv[i], &info) < 0) {
      std::string msg = std::string("rm: cannot remove '") + argv[i] + "': No such file or directory\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }

    if (info.type == LFS_TYPE_DIR) {
      if (!recursive) {
        std::string msg = std::string("rm: cannot remove '") + argv[i] + "': Is a directory\n";
        Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
        any_error = true;
        continue;
      }
      traversal_queue.push(argv[i]);
    } else {
      files_to_remove.push_back(argv[i]);
    }
  }

  // BFS Traversal
  while (!traversal_queue.empty()) {
    coro_yield(gspoon::g_spoon_coro);
    std::string current_dir = traversal_queue.front();
    traversal_queue.pop();
    dirs_to_remove.push_back(current_dir);

    vfs_dir_t dir;
    if (vfs_dir_open(&dir, current_dir.c_str()) < 0) {
      std::string msg = "rm: cannot open directory " + current_dir + "\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
      continue;
    }

    struct vfs_info info;
    while (vfs_dir_read(&dir, &info) > 0) {
      if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
      
      std::string path = current_dir;
      if (!path.empty() && path.back() != '/') path += "/";
      path += info.name;

      if (info.type == LFS_TYPE_DIR) {
        traversal_queue.push(path);
      } else {
        files_to_remove.push_back(path);
      }
    }
    vfs_dir_close(&dir);
  }

  // Remove files
  for (const auto& file : files_to_remove) {
    coro_yield(gspoon::g_spoon_coro);
    sleep_ms(1); // Give USB time to breathe
    if (vfs_remove(file.c_str()) < 0) {
      std::string msg = "rm: cannot remove '" + file + "'\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
    }
  }

  // Remove directories in reverse order (bottom-up)
  for (auto it = dirs_to_remove.rbegin(); it != dirs_to_remove.rend(); ++it) {
    coro_yield(gspoon::g_spoon_coro);
    sleep_ms(1);
    if (vfs_remove(it->c_str()) < 0) {
      std::string msg = "rm: cannot remove '" + *it + "'\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      any_error = true;
    }
  }

  return any_error ? TCL_ERROR : TCL_OK;
}

extern "C" int cat_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: cat [-n] filename...", TCL_STATIC);
    return TCL_ERROR;
  }

  bool print_lines = false;
  int start_idx = 1;

  if (argc > 1 && strcmp(argv[1], "-n") == 0) {
    print_lines = true;
    start_idx = 2;
    if (argc < 3) {
      Tcl_SetResult(interp, (char*)"Usage: cat [-n] filename...", TCL_STATIC);
      return TCL_ERROR;
    }
  }

  Tcl_ResetResult(interp);
  int line_num = 1;
  bool at_line_start = true;
  bool output_started = false;

  for (int i = start_idx; i < argc; i++) {
    coro_yield(gspoon::g_spoon_coro);
    vfs_file_t file;
    int err = vfs_file_open(&file, argv[i], LFS_O_RDONLY);
    if (err < 0) {
      std::string msg =
          std::string("cat: ") + argv[i] + ": No such file or directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }

    char buf[64];
    while (true) {
      coro_yield(gspoon::g_spoon_coro);
      lfs_ssize_t res = vfs_file_read(&file, buf, sizeof(buf));
      if (res < 0) {
        vfs_file_close(&file);
        Tcl_SetResult(interp, (char*)"Error reading file", TCL_STATIC);
        return TCL_ERROR;
      }
      if (res == 0) break;

      if (!print_lines) {
        // Fast path for normal cat
        char tmp[65];
        memcpy(tmp, buf, res);
        tmp[res] = '\0';
        Tcl_AppendResult(interp, tmp, (char*)NULL);
        if (res > 0) output_started = true;
      } else {
        // Slow path for -n
        for (lfs_ssize_t j = 0; j < res; j++) {
          char ch = buf[j];
          if (at_line_start) {
            char numbuf[16];
            snprintf(numbuf, sizeof(numbuf), "%6d  ", line_num++);
            Tcl_AppendResult(interp, numbuf, (char*)NULL);
            at_line_start = false;
          }
          char str[2] = {ch, 0};
          Tcl_AppendResult(interp, str, (char*)NULL);
          if (ch == '\n') at_line_start = true;
          output_started = true;
        }
      }
    }
    vfs_file_close(&file);
  }

  // To strip trailing newline if desired, we can manipulate interp->result
  // directly, but standard cat usually outputs exact bytes anyway. If we really
  // wanted to remove it:
  if (output_started) {
    size_t len = strlen(interp->result);
    if (len > 0 && interp->result[len - 1] == '\n') {
      interp->result[len - 1] = '\0';
    }
  }

  return TCL_OK;
}

extern "C" int wc_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: wc file...", TCL_STATIC);
    return TCL_ERROR;
  }

  Tcl_ResetResult(interp);
  int total_lines = 0;
  int total_words = 0;
  int total_chars = 0;
  bool output_started = false;

  for (int i = 1; i < argc; i++) {
    coro_yield(gspoon::g_spoon_coro);
    vfs_file_t file;
    int err = vfs_file_open(&file, argv[i], LFS_O_RDONLY);
    if (err < 0) {
      std::string msg =
          std::string("wc: ") + argv[i] + ": No such file or directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }

    int lines = 0;
    int words = 0;
    int chars = 0;
    bool in_word = false;

    char buf[64];
    while (true) {
      lfs_ssize_t res = vfs_file_read(&file, buf, sizeof(buf));
      if (res < 0) {
        vfs_file_close(&file);
        Tcl_SetResult(interp, (char*)"Error reading file", TCL_STATIC);
        return TCL_ERROR;
      }
      if (res == 0) break;
      for (lfs_ssize_t j = 0; j < res; j++) {
        char ch = buf[j];
        chars++;
        if (ch == '\n') lines++;
        if (std::isspace(static_cast<unsigned char>(ch))) {
          in_word = false;
        } else if (!in_word) {
          in_word = true;
          words++;
        }
      }
    }
    vfs_file_close(&file);

    total_lines += lines;
    total_words += words;
    total_chars += chars;

    if (output_started) Tcl_AppendResult(interp, "\n", (char*)NULL);
    char outbuf[128];
    snprintf(outbuf, sizeof(outbuf), "%7d %7d %7d %s", lines, words, chars,
             argv[i]);
    Tcl_AppendResult(interp, outbuf, (char*)NULL);
    output_started = true;
  }

  if (argc > 2) {
    if (output_started) Tcl_AppendResult(interp, "\n", (char*)NULL);
    char outbuf[128];
    snprintf(outbuf, sizeof(outbuf), "%7d %7d %7d total", total_lines,
             total_words, total_chars);
    Tcl_AppendResult(interp, outbuf, (char*)NULL);
  }

  return TCL_OK;
}

extern "C" int cd_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    vfs_cwd = "/";
  } else {
    std::string new_cwd = vfs_normalize_path(argv[1]);
    
#if IGNORE_CASE_IN_VFS
    std::string resolved_cwd;
    auto node = vfs_resolve(new_cwd, &resolved_cwd);
    
    struct vfs_info info;
    if (!node || node->stat(&info) < 0 || info.type != LFS_TYPE_DIR) {
      std::string msg = std::string("cd: ") + argv[1] + ": Not a directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }
    vfs_cwd = resolved_cwd;
#else
    struct vfs_info info;
    if (vfs_stat(new_cwd, &info) < 0 || info.type != LFS_TYPE_DIR) {
      std::string msg = std::string("cd: ") + argv[1] + ": Not a directory";
      Tcl_SetResult(interp, const_cast<char*>(msg.c_str()), TCL_VOLATILE);
      return TCL_ERROR;
    }
    vfs_cwd = new_cwd;
#endif
  }
  return TCL_OK;
}

extern "C" int pwd_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  Tcl_SetResult(interp, const_cast<char*>(vfs_cwd.c_str()), TCL_VOLATILE);
  return TCL_OK;
}

static long compute_du(const std::string& path) {
    coro_yield(gspoon::g_spoon_coro);
  std::string norm_path = vfs_normalize_path(path);
  
  // If the path is under /pc, report 0 and do not recurse.
  if (norm_path == "/pc" || norm_path.rfind("/pc/", 0) == 0) {
    return 0;
  }
  
  struct vfs_info info;
  int res = vfs_stat(norm_path, &info);
  if (res < 0) return -1;
  
  if (info.type == LFS_TYPE_REG) {
    // Files < ~128 bytes are typically inlined into the directory block,
    // taking 0 extra blocks of storage.
    if (info.size < 128) return 0; 
    // CTZ skip-list takes roughly 8 bytes per block on average.
    long payload_per_block = lfs.block_size - 8;
    long blocks = (info.size + payload_per_block - 1) / payload_per_block;
    return blocks * lfs.block_size;
  } else if (info.type == LFS_TYPE_DIR) {
    long total = 2 * lfs.block_size; // 2 blocks per directory pair
    vfs_dir_t dir;
    if (vfs_dir_open(&dir, norm_path) == 0) {
      while (vfs_dir_read(&dir, &info) > 0) {
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
        
        std::string child_path;
        if (norm_path == "/") {
            child_path = "/" + std::string(info.name);
        } else {
            child_path = norm_path + "/" + std::string(info.name);
        }
        
        long child_sz = compute_du(child_path);
        if (child_sz > 0) total += child_sz;
      }
      vfs_dir_close(&dir);
    }
    return total;
  }
  return 0;
}

extern "C" int du_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc == 1) {
    // If no arguments, default to "."
    long sz = compute_du(".");
    if (sz < 0) {
      Tcl_SetResult(interp, (char*)"Error computing size", TCL_STATIC);
      return TCL_ERROR;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%-6ldK  .", sz / 1024);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
  }

  std::string result_str;
  for (int i = 1; i < argc; i++) {
    long sz = compute_du(argv[i]);
    if (sz < 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Error reading %s\n", argv[i]);
      result_str += buf;
      continue;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%-6ldK  %s\n", sz / 1024, argv[i]);
    result_str += buf;
  }
  if (!result_str.empty() && result_str.back() == '\n') {
    result_str.pop_back();
  }
  
  Tcl_SetResult(interp, const_cast<char*>(result_str.c_str()), TCL_VOLATILE);
  return TCL_OK;
}

extern "C" int df_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  lfs_ssize_t used_blocks = lfs_fs_size(&lfs_volume);
  if (used_blocks < 0) {
    Tcl_SetResult(interp, (char*)"Error getting filesystem size", TCL_STATIC);
    return TCL_ERROR;
  }
  unsigned long total = lfs.block_count;
  long used = used_blocks;
  long free = total - used;
  unsigned long bsize = lfs.block_size;
  
  long used_pct_int = total ? (used * 1000 / total) : 0;
  long free_pct_int = total ? (free * 1000 / total) : 0;

  char buf[256];
  snprintf(buf, sizeof(buf), 
           "Total: %-6lu K (%5lu Blocks) 100.0%%\n"
           "Used:  %-6ld K (%5ld Blocks) %3ld.%ld%%\n"
           "Free:  %-6ld K (%5ld Blocks) %3ld.%ld%%\n"
           "Block size: %lu bytes",
           (total * bsize) / 1024, total,
           (used * bsize) / 1024, used, used_pct_int / 10, used_pct_int % 10,
           (free * bsize) / 1024, free, free_pct_int / 10, free_pct_int % 10,
           bsize);
  Tcl_SetResult(interp, buf, TCL_VOLATILE);
  return TCL_OK;
}

// "fs" — shell-like wrapper that globs arguments and supports >file
// redirection.
//
// Usage: fs cat *.txt >output.txt
//        fs dir /data >listing
//        fs echo hello world
//
// 1. Scans for ">" redirect: either ">filename" or "> filename"
// 2. Globs remaining arguments (no-match keeps original word, like sh)
// 3. Builds a Tcl command string and Tcl_Eval's it
// 4. On success with redirect: writes result to the file
// 5. Returns the Tcl result and error code
extern "C" int fs_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_SetResult(interp, (char*)"Usage: fs command [args...] [>file]",
                  TCL_STATIC);
    return TCL_ERROR;
  }

  // Pass 1: extract redirect and collect raw arguments
  std::string redirect_file;
  std::vector<std::string> raw_args;

  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    if (arg[0] == '>' && arg[1] != '\0') {
      // ">filename" form
      redirect_file = &arg[1];
    } else if (arg[0] == '>' && arg[1] == '\0') {
      // ">" "filename" form
      if (i + 1 < argc) {
        redirect_file = argv[++i];
      } else {
        Tcl_SetResult(interp, (char*)"> requires a filename", TCL_STATIC);
        return TCL_ERROR;
      }
    } else {
      raw_args.push_back(arg);
    }
  }

  if (raw_args.empty()) {
    Tcl_SetResult(interp, (char*)"fs: no command specified", TCL_STATIC);
    return TCL_ERROR;
  }

  // Pass 2: glob each argument (except the command name)
  std::vector<std::string> expanded;
  expanded.push_back(raw_args[0]);  // Don't glob the command name
  for (size_t i = 1; i < raw_args.size(); i++) {
    std::vector<std::string> matches = glob(raw_args[i]);
    if (matches.empty()) {
      expanded.push_back(raw_args[i]);  // No match → keep original
    } else {
      for (const auto& m : matches) {
        expanded.push_back(m);
      }
    }
  }

  // Pass 3: build a Tcl command string (list-style quoting)
  std::string tcl_cmd;
  for (size_t i = 0; i < expanded.size(); i++) {
    if (i > 0) tcl_cmd += " ";
    // Brace-quote each word to protect special characters
    tcl_cmd += "{";
    tcl_cmd += expanded[i];
    tcl_cmd += "}";
  }

  // Pass 4: evaluate
  int rc = Tcl_Eval(interp, const_cast<char*>(tcl_cmd.c_str()), 0, (char**)0);

  // Pass 5: if redirect and success, write result to file
  if (rc == TCL_OK && !redirect_file.empty()) {
    if (!IsNiceFilename(redirect_file.c_str())) {
      Tcl_SetResult(interp, (char*)"fs: redirect filename contains characters that are not nice", TCL_STATIC);
      return TCL_ERROR;
    }
    const char* result = interp->result;
    vfs_file_t file;
    int err = vfs_file_open(&file, redirect_file,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
      Tcl_SetResult(interp, (char*)"fs: failed to open redirect file",
                    TCL_STATIC);
      return TCL_ERROR;
    }
    if (result && result[0]) {
      int w1 = vfs_file_write(&file, result, strlen(result));
      int w2 = vfs_file_write(&file, "\n", 1);
      if (w1 < 0 || w2 < 0) {
        vfs_file_close(&file);
        Tcl_SetResult(interp, (char*)"fs: failed to write to redirect file", TCL_STATIC);
        return TCL_ERROR;
      }
    }
    vfs_file_close(&file);
    Tcl_SetResult(interp, (char*)"", TCL_STATIC);
  }

  // Result and error code are already set by Tcl_Eval
  return rc;
}

extern "C" int find_cmd(ClientData, Tcl_Interp* interp, int argc, char* argv[]) {
  std::vector<std::string> targets;
  int type_filter = -1; // -1: all, LFS_TYPE_REG: files, LFS_TYPE_DIR: dirs

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) {
      if (strcmp(argv[i+1], "f") == 0) {
        type_filter = LFS_TYPE_REG;
      } else if (strcmp(argv[i+1], "d") == 0) {
        type_filter = LFS_TYPE_DIR;
      } else {
        Tcl_SetResult(interp, (char*)"find: invalid -type argument. Use 'f' or 'd'", TCL_STATIC);
        return TCL_ERROR;
      }
      i++; // skip next arg
    } else {
      targets.push_back(argv[i]);
    }
  }

  if (targets.empty()) {
    Tcl_SetResult(interp, (char*)"Usage: find path... [-type f|d]", TCL_STATIC);
    return TCL_ERROR;
  }

  Tcl_ResetResult(interp);
  std::queue<std::string> dir_queue;

  // Process initial targets
  for (size_t i = 0; i < targets.size(); i++) {
    coro_yield(gspoon::g_spoon_coro);
    const std::string& path = targets[i];
    
    struct vfs_info stat_info;
    if (vfs_stat(path.c_str(), &stat_info) < 0) {
      std::string msg = std::string("find: '") + path + "': No such file or directory\n";
      Tcl_AppendResult(interp, msg.c_str(), (char*)NULL);
      return TCL_ERROR;
    }

    if (type_filter == -1 || stat_info.type == type_filter) {
      Tcl_AppendElement(interp, (char*)path.c_str(), 0);
    }

    if (stat_info.type == LFS_TYPE_DIR) {
      dir_queue.push(path);
    }
  }

  // BFS Traversal
  while (!dir_queue.empty()) {
    coro_yield(gspoon::g_spoon_coro);
    std::string current_dir = dir_queue.front();
    dir_queue.pop();

    vfs_dir_t dir;
    if (vfs_dir_open(&dir, current_dir.c_str()) != 0) {
      continue;
    }

    struct Entry {
      std::string name;
      int type;
    };
    std::vector<Entry> entries;
    struct vfs_info info;

    while (vfs_dir_read(&dir, &info) > 0) {
      if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
      entries.push_back({info.name, info.type});
    }
    vfs_dir_close(&dir);

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
      return a.name < b.name;
    });

    for (const auto& e : entries) {
      coro_yield(gspoon::g_spoon_coro);
      std::string full_path = current_dir;
      if (!full_path.empty() && full_path.back() != '/') {
        full_path += "/";
      }
      full_path += e.name;

      if (type_filter == -1 || e.type == type_filter) {
        Tcl_AppendElement(interp, (char*)full_path.c_str(), 0);
      }

      if (e.type == LFS_TYPE_DIR) {
        dir_queue.push(full_path);
      }
    }
  }

  return TCL_OK;
}

void init_lfs() {
  int err = lfs_mount(&lfs_volume, &lfs);
  if (err) {
    cobs_printf("Formatting littlefs\n");
    lfs_format(&lfs_volume, &lfs);
    err = lfs_mount(&lfs_volume, &lfs);
    if (err) {
      cobs_printf("*** CANNOT FORMAT AND MOUNT littlefs\n");
    }
  } else {
    cobs_printf("Mounted littlefs\n");
  }
}

#endif  // FIRMWARE_PIO_LITTLEFS_H_
