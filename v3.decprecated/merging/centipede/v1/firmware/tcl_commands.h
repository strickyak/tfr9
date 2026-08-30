#ifndef FIRMWARE_PIO_TCL_COMMANDS_H_
#define FIRMWARE_PIO_TCL_COMMANDS_H_

#include <string.h>

#include "../tcl6.7c/tcl.h"
#include "flash-label.h"
#include "littlefs.h"
#include "heuristic_file.h"
#include "egg.h"
#include "cobs_tx.h"
#include "console.h"
#include "../miniz/miniz.h"
extern "C" {
#include "../tcl6.7c/regexp.h"
}
#include "editor.h"
#include "menu.h"
#include "restart.h"
#include "rtc.h"
#include "vfs_rpc.h"
#include "keyboard_injector.h"

static int dummy_traverse_cb(void *data, lfs_block_t block) {
  int* count = (int*)data;
  (*count)++;
  return 0;
}

int centipede_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                  char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " bootmode|restart|reflash|reformat-flash-filesystem|flash-filesystem-stats|stacksize|type\"", NULL);
    return TCL_ERROR;
  }

  if (strcmp(argv[1], "bootmode") == 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)boot_mode);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
  } else if (strcmp(argv[1], "restart") == 0) {
    rp2350_reset_standard();
  } else if (strcmp(argv[1], "reflash") == 0) {
    rp2350_reset_to_flash_mode();
  } else if (strcmp(argv[1], "stacksize") == 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "used: %u free: %u", 
             (unsigned)coro_stack_used(rpc::g_vfs_coro), 
             (unsigned)coro_stack_free(rpc::g_vfs_coro));
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
  } else if (strcmp(argv[1], "type") == 0) {
    if (argc < 3) {
      Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " type <string>\"", NULL);
      return TCL_ERROR;
    }
    keyboard_injector::queue_string(argv[2]);
    return TCL_OK;
  } else if (strcmp(argv[1], "reformat-flash-filesystem") == 0) {
    if (argc < 3 || strcmp(argv[2], "-force")!=0) {
      Tcl_SetResult(interp, const_cast<char*>("Add a final word \"-force\" to your command, if you are sure you want to reformat."), TCL_STATIC);
      return TCL_ERROR;
    }

    static char *cd_slash[3] = {const_cast<char*>("cd"), const_cast<char*>("/"), nullptr};
    cd_cmd(clientData, interp, 2, cd_slash);

    lfs_unmount(&lfs_volume);
    lfs_format(&lfs_volume, &lfs);
    int err = lfs_mount(&lfs_volume, &lfs);
    if (err) {
      Tcl_SetResult(interp, const_cast<char*>("error mounting after format"), TCL_STATIC);
      return TCL_ERROR;
    }
  } else if (strcmp(argv[1], "flash-filesystem-stats") == 0) {
    int used_blocks = 0;
    int err = lfs_fs_traverse(&lfs_volume, dummy_traverse_cb, &used_blocks);
    if (err < 0) {
      char errbuf[64];
      snprintf(errbuf, sizeof(errbuf), "lfs_fs_traverse error %d", err);
      Tcl_SetResult(interp, errbuf, TCL_VOLATILE);
      return TCL_ERROR;
    }
    
    struct lfs_fsinfo fsinfo;
    err = lfs_fs_stat(&lfs_volume, &fsinfo);
    if (err < 0) {
      char errbuf[64];
      snprintf(errbuf, sizeof(errbuf), "lfs_fs_stat error %d", err);
      Tcl_SetResult(interp, errbuf, TCL_VOLATILE);
      return TCL_ERROR;
    }
    
    char buf[256];
    snprintf(buf, sizeof(buf), 
             "disk_version: %u\nblock_size: %u\nblock_count: %u\nused_blocks: %d\nname_max: %u\nfile_max: %u\nattr_max: %u",
             fsinfo.disk_version, fsinfo.block_size, fsinfo.block_count, used_blocks,
             fsinfo.name_max, fsinfo.file_max, fsinfo.attr_max);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
  } else if (strcmp(argv[1], "egg") == 0) {
    // PMODE 3 SAM configuration (6KB mode, offset 0x0800).
    // F2=1, rest Fx=0 -> Offset 0x0800.
    console::poke(0xFFC6, 0); // F0=0
    console::poke(0xFFC8, 0); // F1=0
    console::poke(0xFFCB, 0); // F2=1
    console::poke(0xFFCC, 0); // F3=0
    console::poke(0xFFCE, 0); // F4=0
    console::poke(0xFFD0, 0); // F5=0
    console::poke(0xFFD2, 0); // F6=0

    // V2=1, V1=1, V0=0 -> 6KB VDG mode.
    console::poke(0xFFC5, 0); // V2=1
    console::poke(0xFFC3, 0); // V1=1
    console::poke(0xFFC0, 0); // V0=0

    // PMODE 3 VDG configuration (PIA0 Port B at 0xFF22)
    // INT/EXT=0 (bit 7), GM2..0=110 (bits 6,5,4), CSS=1 (bit 3) -> 0x68
    byte p = console::peek(0xFF22);
    p = (p & 0x07) | 0x80 | 0x68;
    console::poke(0xFF22, p);

    // Poke the egg data
    for (int i = 0; i < 6144; i++) {
        console::poke(0x0800 + i, egg[i]);
    }

    // Hang forever
    while (1) {
        coro_yield(gspoon::g_spoon_coro);
    }
  } else if (strcmp(argv[1], "debug-call-panic") == 0) {
    panic("debug-call-panic");
  } else if (strcmp(argv[1], "debug-call-abort") == 0) {
    centipede_abort("debug-call-abort");
#ifdef TCL_MEM_DEBUG
  } else if (strcmp(argv[1], "debug-malloc-oom") == 0) {
    // Exhaust heap to test OOM panic handling.
    for (int i = 1; ; i++) {
      char* p = ckalloc(256);
      if (!p) centipede_abort("ckalloc returned NULL");
      cobs_printf("ckalloc #%d => %p\n", i, p);
    }
#endif
  } else {
    Tcl_AppendResult(interp, "bad option \"", argv[1],
                     "\": must be restart, reflash, reformat-flash-filesystem, "
                     "flash-filesystem-stats, debug-call-panic, debug-call-abort"
#ifdef TCL_MEM_DEBUG
                     ", debug-malloc-oom"
#endif
                     , NULL);
    return TCL_ERROR;
  }
  return TCL_OK;
}

int file_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
               char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " filename ?filename...?\"", NULL);
    return TCL_ERROR;
  }

  for (int i = 1; i < argc; i++) {
    auto node = vfs_resolve(argv[i]);
    const char* type = HeuristicFileType(node);
    if (argc == 2) {
      Tcl_AppendResult(interp, (char*)type, NULL);
    } else {
      Tcl_AppendResult(interp, argv[i], ": ", (char*)type, "\n", NULL);
    }
  }

  return TCL_OK;
}

int nice_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
               char* argv[]) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " filename\"", NULL);
    return TCL_ERROR;
  }

  if (IsNiceFilename(argv[1])) {
    Tcl_SetResult(interp, (char*)"1", TCL_STATIC);
  } else {
    Tcl_SetResult(interp, (char*)"0", TCL_STATIC);
  }
  return TCL_OK;
}

int hd_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "Usage: hd filename", NULL);
    return TCL_ERROR;
  }
  const char* filename = argv[1];
  vfs_file_t file;
  if (vfs_file_open(&file, filename, LFS_O_RDONLY) < 0) {
    Tcl_AppendResult(interp, "hd: ", filename, ": No such file or directory\n", NULL);
    return TCL_ERROR;
  }
  
  unsigned char buf[16];
  uint32_t offset = 0;
  char outbuf[128];
  
  while (true) {
    lfs_ssize_t bytes = vfs_file_read(&file, buf, sizeof(buf));
    if (bytes <= 0) break;
    
    snprintf(outbuf, sizeof(outbuf), "%08lx  ", (unsigned long)offset);
    std::string line = outbuf;
    
    for (int i = 0; i < 16; i++) {
      if (i < bytes) {
        snprintf(outbuf, sizeof(outbuf), "%02x ", buf[i]);
        line += outbuf;
      } else {
        line += "   ";
      }
      if (i == 7) line += " ";
    }
    
    line += " |";
    for (int i = 0; i < bytes; i++) {
      if (buf[i] >= 32 && buf[i] < 127) {
        line += (char)buf[i];
      } else {
        line += ".";
      }
    }
    line += "|\n";
    
    Tcl_AppendResult(interp, line.c_str(), NULL);
    offset += bytes;
  }
  
  vfs_file_close(&file);
  return TCL_OK;
}

int head_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  int n = 10;
  int arg_idx = 1;
  if (argc > 1 && argv[1][0] == '-') {
    n = atoi(argv[1] + 1);
    arg_idx = 2;
  }
  if (argc - arg_idx != 1) {
    Tcl_AppendResult(interp, "Usage: head [-N] filename", NULL);
    return TCL_ERROR;
  }
  const char* filename = argv[arg_idx];
  vfs_file_t file;
  if (vfs_file_open(&file, filename, LFS_O_RDONLY) < 0) {
    Tcl_AppendResult(interp, "head: ", filename, ": No such file or directory\n", NULL);
    return TCL_ERROR;
  }
  char buf[64];
  std::string line;
  int count = 0;
  while (count < n) {
    lfs_ssize_t bytes = vfs_file_read(&file, buf, sizeof(buf));
    if (bytes <= 0) break;
    for (lfs_ssize_t i = 0; i < bytes; i++) {
      if (buf[i] == '\n') {
        line += '\n';
        Tcl_AppendResult(interp, line.c_str(), NULL);
        line.clear();
        count++;
        if (count >= n) break;
      } else if (buf[i] != '\r') {
        line += buf[i];
      }
    }
  }
  if (count < n && !line.empty()) {
    Tcl_AppendResult(interp, line.c_str(), "\n", NULL);
  }
  vfs_file_close(&file);
  return TCL_OK;
}

int tail_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  int n = 10;
  int arg_idx = 1;
  if (argc > 1 && argv[1][0] == '-') {
    n = atoi(argv[1] + 1);
    arg_idx = 2;
  }
  if (argc - arg_idx != 1) {
    Tcl_AppendResult(interp, "Usage: tail [-N] filename", NULL);
    return TCL_ERROR;
  }
  const char* filename = argv[arg_idx];
  vfs_file_t file;
  if (vfs_file_open(&file, filename, LFS_O_RDONLY) < 0) {
    Tcl_AppendResult(interp, "tail: ", filename, ": No such file or directory\n", NULL);
    return TCL_ERROR;
  }
  std::vector<std::string> lines;
  char buf[64];
  std::string current_line;
  while (true) {
    lfs_ssize_t bytes = vfs_file_read(&file, buf, sizeof(buf));
    if (bytes <= 0) break;
    for (lfs_ssize_t i = 0; i < bytes; i++) {
      if (buf[i] == '\n') {
        lines.push_back(current_line);
        current_line.clear();
        if ((int)lines.size() > n) {
          lines.erase(lines.begin());
        }
      } else if (buf[i] != '\r') {
        current_line += buf[i];
      }
    }
  }
  if (!current_line.empty()) {
    lines.push_back(current_line);
    if ((int)lines.size() > n) {
      lines.erase(lines.begin());
    }
  }
  for (const auto& l : lines) {
    Tcl_AppendResult(interp, l.c_str(), "\n", NULL);
  }
  vfs_file_close(&file);
  return TCL_OK;
}

int grep_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  bool opt_n = false;
  bool opt_v = false;
  bool opt_l = false;
  bool opt_i = false;
  int arg_idx = 1;
  while (arg_idx < argc && argv[arg_idx][0] == '-') {
    for (int j = 1; argv[arg_idx][j] != '\0'; j++) {
      if (argv[arg_idx][j] == 'n') opt_n = true;
      else if (argv[arg_idx][j] == 'v') opt_v = true;
      else if (argv[arg_idx][j] == 'l') opt_l = true;
      else if (argv[arg_idx][j] == 'i') opt_i = true;
      else {
        Tcl_AppendResult(interp, "grep: invalid option -- '", argv[arg_idx]+j, "'\n", NULL);
        return TCL_ERROR;
      }
    }
    arg_idx++;
  }
  
  if (argc - arg_idx < 2) {
    Tcl_SetResult(interp, (char*)"Usage: grep [-nvli] pattern file...", TCL_STATIC);
    return TCL_ERROR;
  }
  
  const char* raw_pattern = argv[arg_idx++];
  char* pattern = (char*)ckalloc(strlen(raw_pattern) + 1);
  strcpy(pattern, raw_pattern);
  if (opt_i) {
    for (char* p = pattern; *p; p++) {
      if (isupper((unsigned char)*p)) *p = tolower((unsigned char)*p);
    }
  }
  
  regexp* prog = regcomp(pattern);
  ckfree(pattern);
  
  if (!prog) {
    Tcl_SetResult(interp, (char*)"grep: invalid regular expression", TCL_STATIC);
    return TCL_ERROR;
  }
  
  auto check_match = [&](char* l, int len) {
    if (!opt_i) {
      return regexec(prog, l) != 0;
    } else {
      std::string lower_line(l, len);
      for (int k = 0; k < len; k++) {
        lower_line[k] = tolower((unsigned char)lower_line[k]);
      }
      return regexec(prog, const_cast<char*>(lower_line.c_str())) != 0;
    }
  };
  
  bool multiple_files = (argc - arg_idx > 1);
  bool any_error = false;
  
  for (; arg_idx < argc; arg_idx++) {
    const char* filename = argv[arg_idx];
    auto node = vfs_resolve(filename);
    if (!node) {
      Tcl_AppendResult(interp, "grep: ", filename, ": No such file or directory\n", NULL);
      any_error = true;
      continue;
    }
    if (strcmp(HeuristicFileType(node), "text") != 0) {
      continue;
    }
    
    vfs_file_t file;
    int err = vfs_file_open(&file, filename, LFS_O_RDONLY);
    if (err < 0) {
      Tcl_AppendResult(interp, "grep: ", filename, ": No such file or directory\n", NULL);
      any_error = true;
      continue;
    }
    
    std::string line;
    int line_num = 1;
    char buf[64];
    bool last_was_cr = false;
    
    while (true) {
      lfs_ssize_t n = vfs_file_read(&file, buf, sizeof(buf));
      if (n < 0) {
        Tcl_AppendResult(interp, "grep: error reading ", filename, "\n", NULL);
        any_error = true;
        break;
      }
      if (n == 0) {
        if (line.length() > 0) {
          bool matched = check_match(const_cast<char*>(line.c_str()), line.length());
          if (opt_v) matched = !matched;
          if (matched) {
            if (opt_l) {
              Tcl_AppendResult(interp, filename, "\n", NULL);
              break;
            }
            if (multiple_files) {
              Tcl_AppendResult(interp, filename, ":", NULL);
            }
            if (opt_n) {
              char numbuf[32];
              snprintf(numbuf, sizeof(numbuf), "%d:", line_num);
              Tcl_AppendResult(interp, numbuf, NULL);
            }
            Tcl_AppendResult(interp, line.c_str(), "\n", NULL);
          }
        }
        break;
      }
      bool skip_file = false;
      for (lfs_ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n' && last_was_cr) {
          last_was_cr = false;
          continue;
        }
        if (c == '\r') {
          c = '\n';
          last_was_cr = true;
        } else {
          last_was_cr = false;
        }

        if (c == '\n') {
          bool matched = check_match(const_cast<char*>(line.c_str()), line.length());
          if (opt_v) matched = !matched;
          if (matched) {
            if (opt_l) {
              Tcl_AppendResult(interp, filename, "\n", NULL);
              skip_file = true;
              break;
            }
            if (multiple_files) {
              Tcl_AppendResult(interp, filename, ":", NULL);
            }
            if (opt_n) {
              char numbuf[32];
              snprintf(numbuf, sizeof(numbuf), "%d:", line_num);
              Tcl_AppendResult(interp, numbuf, NULL);
            }
            Tcl_AppendResult(interp, line.c_str(), "\n", NULL);
          }
          line.clear();
          line_num++;
        } else {
          line += c;
        }
      }
      if (skip_file) break;
    }
    vfs_file_close(&file);
  }
  
  ckfree((char*)prog);
  
  return any_error ? TCL_ERROR : TCL_OK;
}

int glob_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " pattern ?pattern...?\"", NULL);
    return TCL_ERROR;
  }
  
  Tcl_ResetResult(interp);
  
  for (int i = 1; i < argc; i++) {
    std::vector<std::string> matches = glob(argv[i]);
    for (size_t j = 0; j < matches.size(); j++) {
      Tcl_AppendElement(interp, const_cast<char*>(matches[j].c_str()), 0);
    }
  }
  
  return TCL_OK;
}

int lmap_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  int listArgc, i, v, result;
  char **listArgv;
  int varArgc;
  char **varArgv;
  
  if (argc != 4) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
      " varList list command\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  result = Tcl_SplitList(interp, argv[1], &varArgc, &varArgv);
  if (result != TCL_OK) {
    return result;
  }
  if (varArgc == 0) {
    Tcl_AppendResult(interp, "empty variable list", (char *) NULL);
    ckfree((char *) varArgv);
    return TCL_ERROR;
  }
  
  result = Tcl_SplitList(interp, argv[2], &listArgc, &listArgv);
  if (result != TCL_OK) {
    ckfree((char *) varArgv);
    return result;
  }
  if (listArgc % varArgc != 0) {
    Tcl_AppendResult(interp, "list length is not a multiple of variable list length", (char *) NULL);
    ckfree((char *) varArgv);
    ckfree((char *) listArgv);
    return TCL_ERROR;
  }
  
  std::vector<std::string> results;
  
  for (i = 0; i < listArgc; i += varArgc) {
    for (v = 0; v < varArgc; v++) {
      if (Tcl_SetVar(interp, varArgv[v], listArgv[i + v], 0) == NULL) {
        Tcl_SetResult(interp, (char*)"couldn't set loop variable", TCL_STATIC);
        result = TCL_ERROR;
        goto done;
      }
    }
    
    result = Tcl_Eval(interp, argv[3], 0, (char **) NULL);
    if (result != TCL_OK) {
      if (result == TCL_CONTINUE) {
        result = TCL_OK;
        continue;
      } else if (result == TCL_BREAK) {
        result = TCL_OK;
        break;
      } else if (result == TCL_ERROR) {
        char msg[100];
        sprintf(msg, "\n    (\"lmap\" body line %d)", interp->errorLine);
        Tcl_AddErrorInfo(interp, msg);
        break;
      } else {
        break;
      }
    }
    results.push_back(interp->result ? interp->result : "");
  }
done:
  ckfree((char *) varArgv);
  ckfree((char *) listArgv);
  
  if (result == TCL_OK) {
    Tcl_ResetResult(interp);
    for (size_t j = 0; j < results.size(); j++) {
      Tcl_AppendElement(interp, const_cast<char*>(results[j].c_str()), 0);
    }
  }
  return result;
}


int lzip_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
      " list1 ?list2...?\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  int num_lists = argc - 1;
  std::vector<int> list_lengths(num_lists);
  std::vector<char**> list_elements(num_lists);
  
  int max_length = 0;
  for (int i = 0; i < num_lists; i++) {
    int list_argc;
    char **list_argv;
    int result = Tcl_SplitList(interp, argv[i + 1], &list_argc, &list_argv);
    if (result != TCL_OK) {
      for (int k = 0; k < i; k++) {
        ckfree((char*)list_elements[k]);
      }
      return result;
    }
    list_lengths[i] = list_argc;
    list_elements[i] = list_argv;
    if (list_argc > max_length) {
      max_length = list_argc;
    }
  }
  
  Tcl_ResetResult(interp);
  
  std::vector<char*> sub_argv(num_lists);
  char empty_str[] = "";
  
  for (int i = 0; i < max_length; i++) {
    for (int j = 0; j < num_lists; j++) {
      if (i < list_lengths[j]) {
        sub_argv[j] = list_elements[j][i];
      } else {
        sub_argv[j] = empty_str;
      }
    }
    char *merged = Tcl_Merge(num_lists, sub_argv.data());
    Tcl_AppendElement(interp, merged, 0);
    ckfree(merged);
  }
  
  for (int i = 0; i < num_lists; i++) {
    ckfree((char*)list_elements[i]);
  }
  
  return TCL_OK;
}



int minizip_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 3) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " subcommand zipname ?args...?\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  const char* subcmd = argv[1];
  const char* zipname = argv[2];
  
  vfs_file_t file;
  int err = vfs_file_open(&file, zipname, LFS_O_RDONLY);
  if (err < 0) {
    Tcl_AppendResult(interp, "zip: cannot open ", zipname, (char *) NULL);
    return TCL_ERROR;
  }
  
  struct vfs_info info;
  if (vfs_stat(zipname, &info) < 0) {
    vfs_file_close(&file);
    Tcl_AppendResult(interp, "zip: cannot stat ", zipname, (char *) NULL);
    return TCL_ERROR;
  }
  
  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  zip.m_pRead = littlefs_zip_read_func;
  zip.m_pIO_opaque = file.node.get();
  
  if (!mz_zip_reader_init(&zip, info.size, 0)) {
    vfs_file_close(&file);
    Tcl_AppendResult(interp, "zip: failed to read zip archive", (char *) NULL);
    return TCL_ERROR;
  }
  
  int return_code = TCL_OK;
  
  if (strcmp(subcmd, "names") == 0) {
    Tcl_ResetResult(interp);
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < num_files; i++) {
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) continue;
      Tcl_AppendElement(interp, file_stat.m_filename, 0);
    }
  } else if (strcmp(subcmd, "get") == 0) {
    if (argc != 4) {
      Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                       " get zipname membername\"", (char *) NULL);
      return_code = TCL_ERROR;
    } else {
      const char* membername = argv[3];
      size_t uncomp_size = 0;
      void* pBuf = mz_zip_reader_extract_file_to_heap(&zip, membername, &uncomp_size, 0);
      if (!pBuf) {
        Tcl_AppendResult(interp, "zip: failed to extract member ", membername, (char *) NULL);
        return_code = TCL_ERROR;
      } else {
        char* tcl_buf = (char*)ckalloc(uncomp_size + 1);
        memcpy(tcl_buf, pBuf, uncomp_size);
        tcl_buf[uncomp_size] = '\0';
        Tcl_SetResult(interp, tcl_buf, TCL_VOLATILE);
        ckfree(tcl_buf);
        mz_free(pBuf);
      }
    }
  } else {
    Tcl_AppendResult(interp, "bad option \"", subcmd, "\": must be names or get", (char *) NULL);
    return_code = TCL_ERROR;
  }
  
  mz_zip_reader_end(&zip);
  vfs_file_close(&file);
  
  return return_code;
}

int puts_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 2 && argc != 3) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " s ?fd?\"", NULL);
    return TCL_ERROR;
  }
  
  const char* s = argv[1];
  const char* fd = (argc == 3) ? argv[2] : "tty";
  
  bool out_tether = false;
  bool out_coco = false;
  
  if (strcmp(fd, "tether") == 0) {
    out_tether = true;
  } else if (strcmp(fd, "coco") == 0) {
    out_coco = true;
  } else if (strcmp(fd, "tty") == 0) {
    out_tether = true;
    out_coco = true;
  } else {
    Tcl_AppendResult(interp, "bad fd: must be tether, coco, or tty", NULL);
    return TCL_ERROR;
  }
  
  for (const char* p = s; *p; p++) {
    if (out_tether) cobs_putchar(*p);
    if (out_coco) console::emit_char(*p);
  }
  if (out_tether) cobs_putchar('\n');
  if (out_coco) console::emit_char('\n');
  
  return TCL_OK;
}

static int k_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                 char** argv) {
  if (argc > 1) {
    Tcl_SetResult(interp, (char*)argv[1], TCL_VOLATILE);
  } else {
    Tcl_SetResult(interp, (char*)"", TCL_STATIC);
  }
  return TCL_OK;
}

static int iota_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                    char** argv) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " n\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  int n = atoi(argv[1]);
  if (n < 1) {
    return TCL_OK;
  }
  
  for (int i = 0; i < n; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", i);
    Tcl_AppendElement(interp, buf, 0);
  }
  
  return TCL_OK;
}

static int source_cmd(ClientData clientData, Tcl_Interp* interp, int argc,
                      char** argv) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " fileName\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  const char* filename = argv[1];
  
  struct vfs_info info;
  if (vfs_stat(filename, &info) < 0) {
    Tcl_AppendResult(interp, "couldn't read file \"", filename, "\": no such file or directory", (char*)NULL);
    return TCL_ERROR;
  }
  
  vfs_file_t file;
  int err = vfs_file_open(&file, filename, LFS_O_RDONLY);
  if (err < 0) {
    Tcl_AppendResult(interp, "couldn't read file \"", filename, "\": no such file or directory", (char*)NULL);
    return TCL_ERROR;
  }
  
  size_t size = info.size;
  char* buf = (char*)ckalloc(size + 1);
  
  lfs_ssize_t n = vfs_file_read(&file, buf, size);
  vfs_file_close(&file);
  
  if (n < 0) {
    ckfree(buf);
    Tcl_AppendResult(interp, "error reading file \"", filename, "\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  buf[n] = '\0';
  
  int result = Tcl_Eval(interp, buf, 0, (char**)NULL);
  ckfree(buf);
  
  if (result == TCL_OK) {
    Tcl_SetResult(interp, (char*)"", TCL_STATIC);
  }
  
  return result;
}

static inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static inline void trim_string(std::string &s) {
    auto start = s.begin();
    while (start != s.end() && is_space(*start)) start++;
    s.erase(s.begin(), start);
    auto end = s.rbegin();
    while (end != s.rend() && is_space(*end)) end++;
    s.erase(end.base(), s.end());
}

int multifile_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 5) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " subcommand left right filename ?args...?\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  const char* subcmd = argv[1];
  const char* left = argv[2];
  const char* right = argv[3];
  const char* filename = argv[4];
  
  std::string left_str = left;
  std::string right_str = right;
  
  vfs_file_t file;
  int err = vfs_file_open(&file, filename, LFS_O_RDONLY);
  if (err < 0) {
    Tcl_AppendResult(interp, "multifile: cannot open ", filename, (char *) NULL);
    return TCL_ERROR;
  }
  
  struct vfs_info info;
  if (vfs_stat(filename, &info) < 0) {
    vfs_file_close(&file);
    Tcl_AppendResult(interp, "multifile: cannot stat ", filename, (char *) NULL);
    return TCL_ERROR;
  }
  
  std::string content(info.size, '\0');
  lfs_ssize_t read_res = vfs_file_read(&file, &content[0], info.size);
  vfs_file_close(&file);
  
  if (read_res < 0) {
    Tcl_AppendResult(interp, "multifile: failed to read ", filename, (char *) NULL);
    return TCL_ERROR;
  }
  
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string::npos) end = content.size();
    std::string line = content.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
    start = end + 1;
  }
  
  std::string current_header = "";
  
  if (strcmp(subcmd, "names") == 0) {
    Tcl_ResetResult(interp);
    std::vector<std::string> seen_headers;
    bool empty_has_body = false;
    
    for (std::string line : lines) {
      trim_string(line);
      if (line.empty() || line[0] == '#') continue;
      
      if (line.length() >= left_str.length() + right_str.length() && 
          line.compare(0, left_str.length(), left_str) == 0 &&
          line.compare(line.length() - right_str.length(), right_str.length(), right_str) == 0) {
        std::string header = line.substr(left_str.length(), line.length() - left_str.length() - right_str.length());
        trim_string(header);
        current_header = header;
        bool seen = false;
        for (const auto& h : seen_headers) if (h == header) seen = true;
        if (!seen) seen_headers.push_back(header);
      } else {
        if (current_header.empty()) {
          empty_has_body = true;
        }
      }
    }
    
    if (empty_has_body) {
      Tcl_AppendElement(interp, (char*)"", 0);
    }
    for (const auto& h : seen_headers) {
      Tcl_AppendElement(interp, (char*)h.c_str(), 0);
    }
    
  } else if (strcmp(subcmd, "get") == 0) {
    if (argc != 6) {
      Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                       " get left right filename header\"", (char *) NULL);
      return TCL_ERROR;
    }
    
    const char* target_header = argv[5];
    std::string result_body;
    
    for (std::string line : lines) {
      std::string orig_line = line;
      std::string trimmed = line;
      trim_string(trimmed);
      
      if (trimmed.empty() || trimmed[0] == '#') continue;
      
      if (trimmed.length() >= left_str.length() + right_str.length() && 
          trimmed.compare(0, left_str.length(), left_str) == 0 &&
          trimmed.compare(trimmed.length() - right_str.length(), right_str.length(), right_str) == 0) {
        std::string header = trimmed.substr(left_str.length(), trimmed.length() - left_str.length() - right_str.length());
        trim_string(header);
        current_header = header;
      } else {
        if (current_header == target_header) {
          if (!result_body.empty()) result_body += "\n";
          result_body += orig_line;
        }
      }
    }
    
    Tcl_SetResult(interp, (char*)result_body.c_str(), TCL_VOLATILE);
    
  } else {
    Tcl_AppendResult(interp, "bad option \"", argv[1],
                     "\": must be names or get", (char *) NULL);
    return TCL_ERROR;
  }
  
  return TCL_OK;
}

extern void get_system_time(uint32_t *out_sec, uint32_t *out_ms);

int clock_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc < 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                     " subcommand ?arg ...?\"", (char *) NULL);
    return TCL_ERROR;
  }

  if (strcmp(argv[1], "seconds") == 0) {
    if (argc != 2) {
      Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
                       " seconds\"", (char *) NULL);
      return TCL_ERROR;
    }
    uint32_t secs, ms;
    get_system_time(&secs, &ms);
    char buf[64];
    snprintf(buf, sizeof(buf), "%u %u", (unsigned int)secs, (unsigned int)ms);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
  } else {
    Tcl_AppendResult(interp, "bad option \"", argv[1],
                     "\": must be seconds", (char *) NULL);
    return TCL_ERROR;
  }
  return TCL_OK;
}

int bye_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc > 1) {
    Tcl_SetResult(interp, argv[1], TCL_VOLATILE);
  }
  // TCL_BYE signals "Exit The REPL" to BackgroundSpoonFeeder
  return TCL_BYE;
}

int sleep_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"sleep seconds\"", (char *) NULL);
    return TCL_ERROR;
  }
  
  double seconds;
  if (Tcl_GetDouble(interp, argv[1], &seconds) != TCL_OK) {
    return TCL_ERROR;
  }
  
  gspoon::SleepMillis(gspoon::g_spoon_coro, (uint64_t)(seconds * 1000.0));
  
  return TCL_OK;
}

void register_tcl_commands(Tcl_Interp* interp) {
  // Register commands from littlefs.h
  Tcl_CreateCommand(interp, (char*)"menu", menu_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"clock", clock_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"ls", ls_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"find", find_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"mkdir", mkdir_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"rmdir", rmdir_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"echo", echo_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"cp", cp_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"rsync-a", rsync_a_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"mv", mv_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"rm", rm_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"cat", cat_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"wc", wc_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"cd", cd_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"pwd", pwd_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"df", df_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"du", du_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"fs", fs_cmd, NULL, NULL);

  // Register commands from tcl_commands.h
  Tcl_CreateCommand(interp, (char*)"file", file_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"nice", nice_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"grep", grep_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"puts", puts_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"glob", glob_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"lmap", lmap_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"lzip", lzip_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"zip", minizip_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"multifile", multifile_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"k", k_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"iota", iota_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"source", source_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"edit", editor_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"head", head_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"tail", tail_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"hd", hd_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"centipede", centipede_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"bye", bye_cmd, NULL, NULL);
  Tcl_CreateCommand(interp, (char*)"sleep", sleep_cmd, NULL, NULL);

  // Populate global Tcl array 'Label'
  if (FlashLabel::Label[0] == 'p' && FlashLabel::Label[1] == '\0' &&
      FlashLabel::Label[2] == '1' && FlashLabel::Label[3] == '\0') {
    const char* p = FlashLabel::Label;
    while (*p) {
      const char* q = p + strlen(p) + 1;  // q is the value
      Tcl_SetVar2(interp, (char*)"Label", (char*)p, (char*)q, TCL_GLOBAL_ONLY);
      p = q + strlen(q) + 1;
    }
  }
}

#endif  // FIRMWARE_PIO_TCL_COMMANDS_H_
