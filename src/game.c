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
#include <sys/types.h>
#include <sys/wait.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4
#define EXIT_PACMAN_DIED 5

typedef struct {
    board_t *board;
    int id;
} thread_arg_t;

void* pacman_task(void* arg) {
    thread_arg_t *data = (thread_arg_t *)arg;
    board_t *board = data->board;
    int index = data->id;
    pacman_t * pac = &board->pacmans[index];

    while (board->game_running && pac->alive) {
        command_t cmd;
        cmd.turns = 1;

        if (pac->n_moves > 0) {
            cmd = pac->moves[pac->current_move % pac->n_moves];
        } else {
            char dir = pac->next_direction;
            if (dir != '\0') {
                cmd.command = dir;
            } else {
                cmd.command = '\0';
            }
        }

        if (cmd.command != '\0') {
            int result = move_pacman(board, index, &cmd);
            if (result == REACHED_PORTAL) {
                board->game_running = 0; // Stop the game loop
                pthread_exit(NULL);
            } 
        }

        sleep_ms(board->tempo);
    }
    free(data);
    return NULL;
}

void* ghost_task(void* arg) {
    thread_arg_t *data = (thread_arg_t*)arg;
    board_t *board = data->board;
    int index = data->id;
    ghost_t * ghost = &board->ghosts[index];

    while (board->game_running) {
        command_t *cmd = &ghost->moves[ghost->current_move % ghost->n_moves];

        move_ghost(board, index, cmd);
        sleep_ms(board->tempo);
    }
    free(data);
    return NULL;
} 

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

        if (c.command == 'G') {
            return CREATE_BACKUP;
        }

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

    memset(&game_board, 0, sizeof(board_t));

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
            while ((dp = readdir(dirp)) != NULL) {
                if (dp->d_name[0] == '.') continue;
                const char *ext = strrchr(dp->d_name, '.');
                if (ext && strcmp(ext, ".lvl") == 0) cnt_lvl++;
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
            while ((dp = readdir(dirp)) != NULL && idx < cnt_lvl) {
                if (dp->d_name[0] == '.') continue;
                const char *ext = strrchr(dp->d_name, '.');
                if (ext && strcmp(ext, ".lvl") == 0) {
                    char *path = malloc(strlen(dirpath) + strlen(dp->d_name) + 2);
                    sprintf(path, "%s/%s", dirpath, dp->d_name);
                    lvl_paths[idx++] = path;
                }
            }
            closedir(dirp);
            
        }
    }

    index_lp = 0;
    //bool has_backup = false;
    //bool save_used = false;

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
        
        game_board.game_running = 1;

        if (game_board.n_pacmans > 0) {
            thread_arg_t *arg = malloc(sizeof(thread_arg_t));
            arg->board = &game_board;
            arg->id = 0;
            pthread_create(&game_board.pacmans[0].tid, NULL, pacman_task, arg);
        }

        for (int i = 0; i < game_board.n_ghosts; i++) {
            thread_arg_t *arg = malloc(sizeof(thread_arg_t));
            arg->board = &game_board;
            arg->id = i;
            pthread_create(&game_board.ghosts[i].tid, NULL, ghost_task, arg);
        }

        int level_result = CONTINUE_PLAY;

        while (game_board.game_running) {
            char input = get_input();
            if (input == 'Q') {
                game_board.game_running = 0;
                level_result = QUIT_GAME;
            } else if (input == 'G') {
                game_board.game_running = 0;
                level_result = CREATE_BACKUP;
            } else if (input != '\0' && game_board.n_pacmans > 0) {
                game_board.pacmans[0].next_direction = input;
            }

            if (game_board.n_pacmans > 0 && !game_board.pacmans[0].alive) {
                game_board.game_running = 0;
                level_result = QUIT_GAME;
            }

            draw_board(&game_board, DRAW_MENU);
            refresh_screen();

            sleep_ms(game_board.tempo);
        }

        if (game_board.n_pacmans > 0) {
            pthread_join(game_board.pacmans[0].tid, NULL);
        }
        for (int i = 0; i < game_board.n_ghosts; i++) {
            pthread_join(game_board.ghosts[i].tid, NULL);
        }

        if (level_result == QUIT_GAME) {
            if (game_board.n_pacmans > 0 && !game_board.pacmans[0].alive) {
                draw_board(&game_board, DRAW_GAME_OVER);
            } else {
                draw_board(&game_board, DRAW_GAME_OVER);
            }
            refresh_screen();
            sleep_ms(2000);
            end_game = true;
        } else {
            accumulated_points = game_board.pacmans[0].points;
            draw_board(&game_board, DRAW_WIN);
            refresh_screen();
            sleep_ms(2000);
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