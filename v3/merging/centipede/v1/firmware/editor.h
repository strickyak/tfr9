#ifndef FIRMWARE_EDITOR_H_
#define FIRMWARE_EDITOR_H_

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "pico/stdlib.h"
#include "gspoon.h" // For tcl_io and inkey_state
#include "vfs.h"
#include "../tcl6.7c/tcl.h"

struct VisualLine {
  int logical_row;
  int start_col;
  int length;
  std::string text;
};

static void compute_layout(const std::vector<std::string>& lines, std::vector<VisualLine>& vlines) {
  vlines.clear();
  for (int r = 0; r < (int)lines.size(); r++) {
    const std::string& line = lines[r];
    if (line.empty()) {
      vlines.push_back({r, 0, 0, ""});
    } else {
      int len = line.length();
      int start = 0;
      int i = 0;
      std::string current_text = "";
      while (i < len) {
        char c = line[i];
        int w = 1;
        if (c == '\t') {
           w = 4 - (current_text.length() % 4);
        }
        
        if (current_text.length() + w > 39) {
          vlines.push_back({r, start, i - start, current_text});
          start = i;
          current_text = "";
          w = (c == '\t') ? 4 : 1;
        }
        
        if (c == '\t') {
          current_text.append(w, ' ');
        } else {
          current_text += c;
        }
        i++;
      }
      if (i > start) {
        vlines.push_back({r, start, i - start, current_text});
      }
    }
  }
}

static int map_vcol_to_edit_col(const std::string& line, int start_col, int length, int target_vcol) {
  int current_vcol = 0;
  int edit_col = start_col;
  while (edit_col < start_col + length) {
    char c = line[edit_col];
    int w = (c == '\t') ? 4 - (current_vcol % 4) : 1;
    if (current_vcol + w > target_vcol) {
      if (target_vcol - current_vcol >= w / 2 && w > 1) {
        // Closer to the right side of the tab
        return edit_col + 1;
      }
      break;
    }
    current_vcol += w;
    edit_col++;
  }
  return edit_col;
}

static int editor_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char** argv) {
  if (argc != 2) {
    Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0], " fileName\"", (char*)NULL);
    return TCL_ERROR;
  }
  
  const char* filename = argv[1];
  std::vector<std::string> lines;
  
  // Read file
  struct vfs_info info;
  if (vfs_stat(filename, &info) == 0) {
    vfs_file_t file;
    if (vfs_file_open(&file, filename, LFS_O_RDONLY) == 0) {
      std::string current_line;
      char buf[64];
      while (true) {
        lfs_ssize_t n = vfs_file_read(&file, buf, sizeof(buf));
        if (n <= 0) break;
        for (lfs_ssize_t i = 0; i < n; i++) {
          if (buf[i] == '\n') {
            lines.push_back(current_line);
            current_line.clear();
          } else if (buf[i] != '\r') {
            current_line += buf[i];
          }
        }
      }
      if (!current_line.empty() || lines.empty()) {
        lines.push_back(current_line);
      }
      vfs_file_close(&file);
    }
  }
  
  if (lines.empty()) {
    lines.push_back("");
  }
  
  int edit_row = 0;
  int edit_col = 0;
  int scroll_vrow = 0;
  std::string message = "";
  
  console::inkey_state iks = {};
  
  std::vector<VisualLine> vlines;
  bool dirty = true;

  
  while (true) {
    if (dirty) {
      compute_layout(lines, vlines);
      
      // Find cursor visual position
      int cursor_vrow = 0;
      int cursor_vcol = 0;
      for (int i = 0; i < (int)vlines.size(); i++) {
        const VisualLine& vl = vlines[i];
        if (vl.logical_row == edit_row) {
          if (edit_col >= vl.start_col && edit_col <= vl.start_col + vl.length) {
            cursor_vrow = i;
            cursor_vcol = 0;
            for (int j = vl.start_col; j < edit_col; j++) {
              char c = lines[edit_row][j];
              if (c == '\t') cursor_vcol += 4 - (cursor_vcol % 4);
              else cursor_vcol++;
            }
            if (cursor_vcol >= 39 && i + 1 < (int)vlines.size() && vlines[i+1].logical_row == edit_row) {
              continue; // Check the next chunk
            }
            break;
          }
        }
      }
      
      if (cursor_vrow < scroll_vrow) {
        scroll_vrow = cursor_vrow;
      } else if (cursor_vrow >= scroll_vrow + 22) {
        scroll_vrow = cursor_vrow - 21;
      }
      
      // Render
      tcl_io::emit_string("\x1b[H"); // Home
      for (int i = 0; i < 22; i++) {
        int vr = scroll_vrow + i;
        if (vr < (int)vlines.size()) {
          if (vr == cursor_vrow) {
            std::string line_text = vlines[vr].text;
            if (cursor_vcol < (int)line_text.length()) {
              tcl_io::emit_string(line_text.substr(0, cursor_vcol).c_str());
              tcl_io::emit_string("\x1b[7m");
              tcl_io::emit_string(line_text.substr(cursor_vcol, 1).c_str());
              tcl_io::emit_string("\x1b[0m");
              tcl_io::emit_string(line_text.substr(cursor_vcol + 1).c_str());
            } else {
              tcl_io::emit_string(line_text.c_str());
              for (int c = line_text.length(); c < cursor_vcol; c++) {
                tcl_io::emit_string(" ");
              }
              tcl_io::emit_string("\x1b[7m \x1b[0m");
            }
          } else {
            tcl_io::emit_string(vlines[vr].text.c_str());
          }
        }
        tcl_io::emit_string("\x1b[K\r\n"); // Clear to end of line and newline
      }
      
      // Line 23: separator
      tcl_io::emit_string("---------------------------------------\x1b[K\r\n");
      // Line 24: message or status
      if (!message.empty()) {
        tcl_io::emit_string(message.c_str());
      } else {
        char status[64];
        snprintf(status, sizeof(status), "ClearS=Save. ClearQ=Quit. Line %d:%d", edit_row + 1, edit_col + 1);
        tcl_io::emit_string(status);
      }
      tcl_io::emit_string("\x1b[K");
      
      // Set cursor
      char cbuf[32];
      snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%df", (cursor_vrow - scroll_vrow) + 1, cursor_vcol + 1);
      tcl_io::emit_string(cbuf);
      
      dirty = false;
      message = "";
    }
    
    byte key = tcl_io::poll_key(&iks);
    if (key == 0) {
      uint64_t start = get_system_ticks_20ms();
      while (get_system_ticks_20ms() == start) {
        coro_yield(gspoon::g_spoon_coro);
      }
      continue;
    }
    
    if (key == 19) { // Ctrl-S (Save and quit)
      vfs_file_t out;
      int err = vfs_file_open(&out, filename, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
      if (err < 0) {
        message = "Error: Could not open file for writing!";
        dirty = true;
        continue;
      }
      for (const std::string& ln : lines) {
        vfs_file_write(&out, ln.c_str(), ln.length());
        vfs_file_write(&out, "\n", 1);
      }
      vfs_file_close(&out);
      tcl_io::emit_string("\x1b[2J\x1b[H");
      return TCL_OK;
    }
    
    if (key == 17) { // Ctrl-Q (Quit)
      tcl_io::emit_string("\x1b[2J\x1b[H");
      return TCL_OK;
    }
    
    if (key == 1) { // Ctrl-A
      edit_col = 0;
      dirty = true;
      continue;
    }
    
    if (key == 5) { // Ctrl-E
      edit_col = lines[edit_row].length();
      dirty = true;
      continue;
    }
    
    if (key == 128) { // Up
      compute_layout(lines, vlines);
      int cursor_vrow = 0;
      int cursor_vcol = 0;
      for (int i = 0; i < (int)vlines.size(); i++) {
        const VisualLine& vl = vlines[i];
        if (vl.logical_row == edit_row) {
          if (edit_col >= vl.start_col && edit_col <= vl.start_col + vl.length) {
            cursor_vrow = i;
            cursor_vcol = 0;
            for (int j = vl.start_col; j < edit_col; j++) {
              char c = lines[edit_row][j];
              if (c == '\t') cursor_vcol += 4 - (cursor_vcol % 4);
              else cursor_vcol++;
            }
            if (cursor_vcol >= 39 && i + 1 < (int)vlines.size() && vlines[i+1].logical_row == edit_row) continue;
            break;
          }
        }
      }
      if (cursor_vrow > 0) {
        cursor_vrow--;
        edit_row = vlines[cursor_vrow].logical_row;
        edit_col = map_vcol_to_edit_col(lines[edit_row], vlines[cursor_vrow].start_col, vlines[cursor_vrow].length, cursor_vcol);
      }
      dirty = true;
      continue;
    }
    if (key == 129) { // Down
      compute_layout(lines, vlines);
      int cursor_vrow = 0;
      int cursor_vcol = 0;
      for (int i = 0; i < (int)vlines.size(); i++) {
        const VisualLine& vl = vlines[i];
        if (vl.logical_row == edit_row) {
          if (edit_col >= vl.start_col && edit_col <= vl.start_col + vl.length) {
            cursor_vrow = i;
            cursor_vcol = 0;
            for (int j = vl.start_col; j < edit_col; j++) {
              char c = lines[edit_row][j];
              if (c == '\t') cursor_vcol += 4 - (cursor_vcol % 4);
              else cursor_vcol++;
            }
            if (cursor_vcol >= 39 && i + 1 < (int)vlines.size() && vlines[i+1].logical_row == edit_row) continue;
            break;
          }
        }
      }
      if (cursor_vrow < (int)vlines.size() - 1) {
        cursor_vrow++;
        edit_row = vlines[cursor_vrow].logical_row;
        edit_col = map_vcol_to_edit_col(lines[edit_row], vlines[cursor_vrow].start_col, vlines[cursor_vrow].length, cursor_vcol);
      }
      dirty = true;
      continue;
    }
    if (key == 131) { // Cursor Right (non-destructive)
      if (edit_col < (int)lines[edit_row].length()) {
        edit_col++;
      } else if (edit_row < (int)lines.size() - 1) {
        edit_row++;
        edit_col = 0;
      }
      dirty = true;
      continue;
    }
    if (key == 130) { // Cursor Left (non-destructive)
      if (edit_col > 0) {
        edit_col--;
      } else if (edit_row > 0) {
        edit_row--;
        edit_col = lines[edit_row].length();
      }
      dirty = true;
      continue;
    }
    if (key == 132) { // Page Up
      edit_row -= 20;
      if (edit_row < 0) edit_row = 0;
      if (edit_col > (int)lines[edit_row].length()) edit_col = lines[edit_row].length();
      dirty = true;
      continue;
    }
    if (key == 133) { // Page Down
      edit_row += 20;
      if (edit_row >= (int)lines.size()) edit_row = lines.size() - 1;
      if (edit_col > (int)lines[edit_row].length()) edit_col = lines[edit_row].length();
      dirty = true;
      continue;
    }
    
    if (key == '\r' || key == '\n') {
      std::string rem = lines[edit_row].substr(edit_col);
      lines[edit_row] = lines[edit_row].substr(0, edit_col);
      lines.insert(lines.begin() + edit_row + 1, rem);
      edit_row++;
      edit_col = 0;
      dirty = true;
    } else if (key == 8 || key == 127) { // Backspace
      if (edit_col > 0) {
        lines[edit_row].erase(edit_col - 1, 1);
        edit_col--;
        dirty = true;
      } else if (edit_row > 0) {
        edit_col = lines[edit_row - 1].length();
        lines[edit_row - 1] += lines[edit_row];
        lines.erase(lines.begin() + edit_row);
        edit_row--;
        dirty = true;
      }
    } else if (key == '\t' || (key >= 32 && key < 127)) { // Printable and Tab
      lines[edit_row].insert(edit_col, 1, (char)key);
      edit_col++;
      dirty = true;
    }
  }
}

#endif // FIRMWARE_EDITOR_H_
