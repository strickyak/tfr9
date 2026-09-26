#ifndef _MENU_H_
#define _MENU_H_

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include "../tcl6.7c/tcl.h"
#include "tcl_io.h"
#include "coro.h"
#include "config.h"

enum class FieldType {
    CHECKBOX,
    DECIMAL,
    FILENAME,
    ACTION
};

struct MenuField {
    char hotkey;
    std::string name;
    FieldType type;
    int width;
    int row; // 0-indexed relative to start of template
    int col; // 0-indexed column of '['
    std::string current_value;
};

struct MenuState {
    std::string title;
    std::vector<std::string> template_lines;
    std::string array_name;
    std::vector<MenuField> fields;
    std::map<char, std::string> actions;
    std::vector<std::vector<char>> at_most_one;
    bool active;
    int active_field_idx;
};

static MenuState g_menu;
namespace gspoon { extern Coro* g_spoon_coro; } // From gspoon.h

int menu_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]);

#endif // _MENU_H_

static void menu_draw_screen() {
        coro_yield(gspoon::g_spoon_coro);
    tcl_io::emit_string("\x1b[2J"); // Clear screen
        coro_yield(gspoon::g_spoon_coro);
    // Draw title
    char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[1;1H%s", g_menu.title.c_str());
        coro_yield(gspoon::g_spoon_coro);
    tcl_io::emit_string(buf);
        coro_yield(gspoon::g_spoon_coro);
    
    // Draw template
    int row = 0;
    for (const auto& line : g_menu.template_lines) {
        coro_yield(gspoon::g_spoon_coro);
        snprintf(buf, sizeof(buf), "\x1b[%d;1H%s", row + 2, line.c_str());
        tcl_io::emit_string(buf);
        row++;
    }
    
    // Draw fields
    for (size_t i = 0; i < g_menu.fields.size(); i++) {
        coro_yield(gspoon::g_spoon_coro);
        const auto& field = g_menu.fields[i];
        bool is_active = ((int)i == g_menu.active_field_idx);
        
        
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", field.row + 2, field.col + 1);
        tcl_io::emit_string(buf);
        
        if (is_active) tcl_io::emit_string("\x1b[7m");
        
        tcl_io::emit_string("[");
        
        std::string val = field.current_value;
        if (field.type == FieldType::CHECKBOX) {
            val = (val == "1") ? "*" : " ";
        } else if (field.type == FieldType::ACTION) {
            val = ":";
        }
        if ((int)val.length() > field.width) val = val.substr(0, field.width);
        while ((int)val.length() < field.width) val += " ";
        
        tcl_io::emit_string(val.c_str());
        tcl_io::emit_string("]");
        
        if (is_active) tcl_io::emit_string("\x1b[0m");
    }
    
    // Position cursor at the end (or out of the way)
    snprintf(buf, sizeof(buf), "\x1b[%d;1H", (int)g_menu.template_lines.size() + 2);
    tcl_io::emit_string(buf);
}

int menu_cmd(ClientData clientData, Tcl_Interp* interp, int argc, char* argv[]) {
    coro_yield(gspoon::g_spoon_coro);
    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"menu subcommand ?arg ...?\". ",
                         "Allowed subcommands: clear, array, title, template, check, decimal, ",
                         "filename, action, at-most-one, store, fetch, just-exit, save-and-exit, render", (char *) NULL);
        return TCL_ERROR;
    }
    
    std::string sub = argv[1];
    
    if (sub == "clear") {
        g_menu = MenuState();
        g_menu.active_field_idx = 0;
    } else if (sub == "array") {
        if (argc != 3) return TCL_ERROR;
        g_menu.array_name = argv[2];
    } else if (sub == "title") {
        if (argc != 3) return TCL_ERROR;
        g_menu.title = argv[2];
    } else if (sub == "template") {
        if (argc != 3) return TCL_ERROR;
        g_menu.template_lines.clear();
        std::string tpl = argv[2];
        std::stringstream ss(tpl);
        std::string line;
        while (std::getline(ss, line, '\n')) {
            if (line.length() >= 3 && line[2] == '|') {
                if (line.length() >= 4) {
                    g_menu.template_lines.push_back(line.substr(4));
                } else {
                    g_menu.template_lines.push_back("");
                }
            }
        }
    } else if (sub == "check" || sub == "decimal" || sub == "filename" || sub == "action") {
        if (argc != 4) return TCL_ERROR;
        char hotkey = argv[2][0];
        std::string name = argv[3];
        
        if (sub == "action") {
            g_menu.actions[hotkey] = argv[3];
        }

        FieldType t = FieldType::FILENAME;
        if (sub == "check") t = FieldType::CHECKBOX;
        if (sub == "decimal") t = FieldType::DECIMAL;
        if (sub == "action") t = FieldType::ACTION;
        
        int r = -1;
        int c = -1;
        int w = 0;
        std::string initial_value = "";
        for (size_t row = 0; row < g_menu.template_lines.size(); row++) {
            const std::string& line = g_menu.template_lines[row];
            for (size_t col = 1; col < line.length(); col++) {
                if (line[col] == '[') {
                    size_t end = line.find(']', col);
                    if (end != std::string::npos) {
                        bool found = false;
                        for (size_t i = col + 1; i < end; i++) {
                            if (line[i] == hotkey) {
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                            r = row;
                            c = col;
                            w = end - col - 1;
                            initial_value = line.substr(col + 1, w);
                            break;
                        }
                    }
                }
            }
            if (r != -1) break;
        }
        
        if (r != -1) {
            g_menu.fields.push_back({hotkey, name, t, w, r, c, (t == FieldType::ACTION) ? initial_value : ""});
        } else {
            // Field not found in template! Ignore or error?
            // Let's just ignore for now or append to bottom.
        }
    } else if (sub == "at-most-one") {
        if (argc != 3) return TCL_ERROR;
        std::vector<char> group;
        for (int i = 0; argv[2][i] != 0; i++) {
            if (argv[2][i] != ' ' && argv[2][i] != '{' && argv[2][i] != '}') {
                group.push_back(argv[2][i]);
            }
        }
        if (!group.empty()) {
            g_menu.at_most_one.push_back(group);
        }
    } else if (sub == "store") {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"menu store arrayName\"", (char *) NULL);
            return TCL_ERROR;
        }
        const char* arr = argv[2];
        centipede_config.SetAll(false);
        const char* val;
        
        val = Tcl_GetVar2(interp, (char*)arr, (char*)"ram_64k", 0);
        if (val && atoi(val) != 0) centipede_config.ram_64k = true;
        
        val = Tcl_GetVar2(interp, (char*)arr, (char*)"rom_disk11", 0);
        if (val && atoi(val) != 0) centipede_config.rom_disk11 = true;
        
        val = Tcl_GetVar2(interp, (char*)arr, (char*)"floppy_fd", 0);
        if (val && atoi(val) != 0) centipede_config.floppy_fd = true;

        val = Tcl_GetVar2(interp, (char*)arr, (char*)"floppy_pc", 0);
        if (val && atoi(val) != 0) centipede_config.floppy_pc = true;

        val = Tcl_GetVar2(interp, (char*)arr, (char*)"trace_writes", 0);
        if (val && atoi(val) != 0) centipede_config.trace_writes = true;
        
        val = Tcl_GetVar2(interp, (char*)arr, (char*)"trace_reads", 0);
        if (val && atoi(val) != 0) centipede_config.trace_reads = true;

        set_floppy_names();
        
    } else if (sub == "fetch") {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"menu fetch arrayName\"", (char *) NULL);
            return TCL_ERROR;
        }
        const char* arr = argv[2];
        Tcl_SetVar2(interp, (char*)arr, (char*)"ram_64k", (char*)(centipede_config.ram_64k ? "1" : "0"), 0);
        Tcl_SetVar2(interp, (char*)arr, (char*)"rom_disk11", (char*)(centipede_config.rom_disk11 ? "1" : "0"), 0);
        Tcl_SetVar2(interp, (char*)arr, (char*)"floppy_fd", (char*)(centipede_config.floppy_fd ? "1" : "0"), 0);
        Tcl_SetVar2(interp, (char*)arr, (char*)"floppy_pc", (char*)(centipede_config.floppy_pc ? "1" : "0"), 0);
        Tcl_SetVar2(interp, (char*)arr, (char*)"trace_writes", (char*)(centipede_config.trace_writes ? "1" : "0"), 0);
        Tcl_SetVar2(interp, (char*)arr, (char*)"trace_reads", (char*)(centipede_config.trace_reads ? "1" : "0"), 0);
    } else if (sub == "just-exit") {
        g_menu.active = false;
    } else if (sub == "save-and-exit") {
        g_menu.active = false;
        for (auto& field : g_menu.fields) {
            if (field.type != FieldType::ACTION) {
                Tcl_SetVar2(interp, (char*)g_menu.array_name.c_str(), (char*)field.name.c_str(), (char*)field.current_value.c_str(), 0);
            }
        }
    } else if (sub == "render") {
        for (auto& field : g_menu.fields) {
            if (field.type == FieldType::ACTION) continue;

            const char* val = Tcl_GetVar2(interp, (char*)g_menu.array_name.c_str(), (char*)field.name.c_str(), 0);
            if (val) {
                field.current_value = val;
            } else {
                if (field.type == FieldType::CHECKBOX) field.current_value = "0";
                else field.current_value = "";
            }
        }
        
        g_menu.active = true;
        console::inkey_state iks = {};
        
        while (g_menu.active) {
            menu_draw_screen();
            
            while (true) {
                byte key = tcl_io::poll_key(&iks);
                if (key == 0) {
                    uint64_t start = get_system_ticks_20ms();
                    while (get_system_ticks_20ms() == start) {
                        coro_yield(gspoon::g_spoon_coro);
                    }
                    continue;
                }
                
                // We got a key!
                bool handled = false;
                
                // Arrow keys
                if (key == 128) { // Up
                    g_menu.active_field_idx--;
                    if (g_menu.active_field_idx < 0) g_menu.active_field_idx = g_menu.fields.size() - 1;
                    handled = true;
                } else if (key == 129) { // Down
                    g_menu.active_field_idx++;
                    if (g_menu.active_field_idx >= (int)g_menu.fields.size()) g_menu.active_field_idx = 0;
                    handled = true;
                }
                
                if (handled) break;
                
                if (g_menu.fields.empty()) break;
                
                auto& field = g_menu.fields[g_menu.active_field_idx];
                
                if (field.type == FieldType::ACTION) {
                    if (key == '\r' || key == '\n' || key == ' ') {
                        int code = Tcl_Eval(interp, (char*)g_menu.actions[field.hotkey].c_str(), 0, NULL);
                        if (code != TCL_OK) return code;
                        break;
                    }
                } else if (field.type == FieldType::CHECKBOX) {
                    if (key == ' ') {
                        field.current_value = (field.current_value == "1") ? "0" : "1";
                        // Handle at-most-one
                        if (field.current_value == "1") {
                            for (const auto& group : g_menu.at_most_one) {
                                bool in_group = false;
                                for (char c : group) if (c == field.hotkey) in_group = true;
                                if (in_group) {
                                    for (auto& f : g_menu.fields) {
                                        if (f.hotkey != field.hotkey) {
                                            for (char c : group) {
                                                if (f.hotkey == c) f.current_value = "0";
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        break;
                    }
                } else {
                    // Editing string
                    if (key == 8 || key == 127) { // backspace
                        if (field.current_value.length() > 0) {
                            field.current_value.pop_back();
                            break;
                        }
                    } else if (key >= 32 && key < 127) {
                        if ((int)field.current_value.length() < field.width) {
                            field.current_value += (char)key;
                            break;
                        }
                    }
                }
            }
        }
        
    } else {
        Tcl_AppendResult(interp, "bad option \"", sub.c_str(), "\": must be clear, array, title, template, check, decimal, filename, action, at-most-one, store, fetch, just-exit, save-and-exit, or render", (char *) NULL);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}
