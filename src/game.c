#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    if (pacman->n_moves == 0) { // if is user input
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

int main(int argc, char** argv) {
    srand((unsigned int)time(NULL));
    board_t game_board;
    open_debug_file("debug.log");

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    bool automatic_mode = true;
    char **lvl_paths = NULL;
    int index_lp = 0;
    int cnt_lvl = 0;

    if (argc == 2) {
        automatic_mode = false;
        const char *dirpath = argv[1];
        debug("Loading levels from directory: %s\n", dirpath);
        
        DIR *dirp = opendir(dirpath);
        if (dirp != NULL) {
            struct dirent *dp;
            // First pass: count levels
            while ((dp = readdir(dirp)) != NULL) {
                if (dp->d_name[0] == '.') continue;
                const char *ext = strrchr(dp->d_name, '.');
                if (ext && strcmp(ext, ".lvl") == 0) {
                    cnt_lvl++;
                }
            }
            closedir(dirp);

            if (cnt_lvl == 0) {
                terminal_cleanup();
                fprintf(stderr, "No .lvl files found in the directory.\n");
                return EXIT_FAILURE;
            }

            lvl_paths = malloc(cnt_lvl * sizeof(char*));
            dirp = opendir(dirpath);
            int idx = 0;
            if (dirp != NULL) {
                while ((dp = readdir(dirp)) != NULL && idx < cnt_lvl) {
                    if (dp->d_name[0] == '.') continue;
                    const char *ext = strrchr(dp->d_name, '.');
                    if (ext && strcmp(ext, ".lvl") == 0) {
                        size_t len = strlen(dirpath) + strlen(dp->d_name) + 2;
                        char *path = malloc(len);
                        sprintf(path, "%s/%s", dirpath, dp->d_name);
                        lvl_paths[idx++] = path;
                    }
                }
                closedir(dirp);
            }
        }
    }

    index_lp = 0;

    while (!end_game) {
        if (!automatic_mode) {
            if (index_lp >= cnt_lvl) {
                end_game = true;
                break;
            }
            load_level_file(&game_board, lvl_paths[index_lp++], 0, accumulated_points);
        } else {
            load_level(&game_board, accumulated_points);
            end_game = true; 
        }
        
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(2000);
                break;
            }

            if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(2000);
                end_game = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 
            accumulated_points = game_board.pacmans[0].points;      
        }
        unload_level(&game_board);
    }    

    if (lvl_paths) {
        for (int i = 0; i < cnt_lvl; i++) {
            free(lvl_paths[i]);
        }
        free(lvl_paths);
    }

    terminal_cleanup();
    close_debug_file();

    return 0;
}