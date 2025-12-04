#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
//#include <stddef.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4
#define STRIDE 128

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

char* parse_line(board_t *board, char *line, int max_files, int max_pacmans) {
    char *files_to_open[max_files];
    int index = 0;
    int n_pacmans = 0;
    if (strncmp(line, "DIM", 3) == 0) {
        sscanf(line + 4, "%d %d", &board->height, &board->width);
    } else if (strncmp(line, "TEMPO", 5) == 0) {
        sscanf(line + 6, "%d", &board->tempo);
    } else if (strncmp(line, "PAC", 3) == 0) {
        char *token = strtok(line + 4, " ");
        while (token != NULL && index < max_pacmans) { // Assuming MAX_PACMANS is defined
            files_to_open[index++] = strdup(token); // Store each filename
            token = strtok(NULL, " ");
        }
        board->n_pacmans = n_pacmans = index; // Update the number of pacmans
    } else if (strncmp(line, "MON", 3) == 0) {
        char *token = strtok(line + 4, " ");
        while (token != NULL) {
            files_to_open[index++] = strdup(token); // Store each filename
            token = strtok(NULL, " ");
        }
        board->n_ghosts = index - n_pacmans; // Update the number of ghosts
    } else {
        //ver isto
        board->board = malloc(board->width * board->height * sizeof(board_pos_t));
    }
    
    return *files_to_open;
}

char* read_file(char* filepath, board_t *board, int max_files, int max_pacmans) {
    // Open the level file for reading
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open level file");
        return NULL;
    }

    // Allocate memory for file content
    char *file_content = malloc(STRIDE);
    if (!file_content) {
        perror("Failed to allocate memory");
        close(fd);
        return NULL;
    }

    ssize_t bytes_read;
    size_t total_read = 0;

    // Read the file content
    while ((bytes_read = read(fd, file_content + total_read, STRIDE - total_read)) > 0) {
        total_read += bytes_read;
        if (total_read >= STRIDE) {
            // Resize if necessary
            char *new_content = realloc(file_content, total_read + STRIDE);
            if (!new_content) {
                perror("Failed to reallocate memory");
                free(file_content);
                close(fd);
                return NULL;
            }
            file_content = new_content;
        }
    }

    if (bytes_read < 0) {
        perror("Failed to read level file");
        free(file_content);
        close(fd);
        return NULL;
    }

    // Process the file content to build the level
    char *line = strtok(file_content, "\n");
    while (line != NULL) {
        if (line[0] != '#') {
            parse_line(board, line, max_files, max_pacmans);
        }
        line = strtok(NULL, "\n");
    }

    free(file_content);
    close(fd);
    return NULL;

}


int main(int argc, char** argv) {
    board_t game_board;

    if (argc == 2) {
        const char *dirpath = argv[1];
        DIR *dirp = opendir(dirpath);
        if (dirp != NULL) {
            int cnt_files = 0;
            int cnt_pacman = 0;
            int cnt_lvl = 0;
            struct dirent *dp;

            // First pass: count files
            while ((dp = readdir(dirp)) != NULL) {
                if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                    continue;

                const char *ext = strrchr(dp->d_name, '.');
                if (!ext) continue;

                if (strcmp(ext, ".lvl") == 0) {
                    cnt_lvl++;
                } else if (strcmp(ext, ".p") == 0) {
                    cnt_pacman++;
                }
                cnt_files++;
            }

            closedir(dirp);

            if (cnt_lvl == 0) {
                fprintf(stderr, "No .lvl files found in the directory.\n");
                return EXIT_FAILURE;
            }
            if (cnt_pacman > cnt_lvl) {
                fprintf(stderr, "More .p files than .lvl files found in the directory.\n");
                return EXIT_FAILURE;
            }

            // Now we can read the files
            dirp = opendir(dirpath);
            if (dirp != NULL) {
                while ((dp = readdir(dirp)) != NULL) {
                    if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
                        continue;

                    const char *ext = strrchr(dp->d_name, '.');
                    if (!ext) continue;

                    size_t dlen = strlen(dirpath);
                    size_t nlen = strlen(dp->d_name);
                    size_t need = dlen + 1 + nlen + 1;
                    char *path = malloc(need);
                    if (!path) continue;
                    strcpy(path, dirpath);
                    if (dlen == 0 || dirpath[dlen-1] != '/') {
                        path[dlen] = '/';
                        path[dlen+1] = '\0';
                    } else {
                        path[dlen] = '\0';
                    }
                    strcat(path, dp->d_name);

                    if (strcmp(ext, ".lvl") == 0) {
                        read_file(path, &game_board, cnt_files, cnt_pacman);
                    }

                    free(path);
                }
                closedir(dirp);
            }
        }
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    

    while (!end_game) {
        load_level(&game_board, accumulated_points); //ESTE É O AUTOMATICO
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                break;
            }

            if(result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        print_board(&game_board);
        unload_level(&game_board);
    }    

    terminal_cleanup();

    close_debug_file();

    return 0;
}
