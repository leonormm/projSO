#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>

#define STRIDE 128

FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    pac->current_move+=1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    debug("Killing %d pacman\n\n", pacman_index);
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    board->board[index].content = ' ';

    // Mark pacman as dead
    pac->alive = 0;
}

// Static Loading
int load_pacman(board_t* board, int points) {
    board->board[1 * board->width + 1].content = 'P'; // Pacman
    board->pacmans[0].pos_x = 1;
    board->pacmans[0].pos_y = 1;
    board->pacmans[0].alive = 1;
    board->pacmans[0].points = points;
    return 0;
}

/**
 * Loads a pacman from file, by reading the file and parsing its content from the buffer
 */
int load_pacman_file(board_t* board, const char* filepath) {
    board->pacmans[0].n_moves = board->cnt_moves;
    char* buffer[MAX_MOVES + 3];
    *buffer = read_file((char*)filepath, board, -1);
    //buffer[0] -> passo
    //buffer[1] -> pos_x
    //buffer[2] -> pos_y
    //buffer[3..] -> moves
    board->pacmans[0].passo = atoi(buffer[0]);
    board->pacmans[0].pos_x = atoi(buffer[1]);
    board->pacmans[0].pos_y = atoi(buffer[2]);
    board->board[board->pacmans[0].pos_y * board->width + board->pacmans[0].pos_x].content = 'P';
    board->pacmans[0].alive = 1;
    board->pacmans[0].points = 0;
    board->pacmans[0].waiting = board->pacmans[0].passo;
    board->pacmans[0].current_move = 0;
    for (int i = 0; i < board->pacmans[0].n_moves; i++) {
        board->pacmans[0].moves[i].command = buffer[3 + i][0];
        board->pacmans[0].moves[i].turns = 1; // Default 1 turn per move
    }
    load_pacman(board, 0);
    return 0;
}

// Static Loading
int load_ghost(board_t* board) {
    // Ghost 0
    board->board[3 * board->width + 1].content = 'M'; // Monster
    board->ghosts[0].pos_x = 1;
    board->ghosts[0].pos_y = 3;
    board->ghosts[0].passo = 0;
    board->ghosts[0].waiting = 0;
    board->ghosts[0].current_move = 0;
    board->ghosts[0].n_moves = 16;
    for (int i = 0; i < 8; i++) {
        board->ghosts[0].moves[i].command = 'D';
        board->ghosts[0].moves[i].turns = 1; 
    }
    for (int i = 8; i < 16; i++) {
        board->ghosts[0].moves[i].command = 'A';
        board->ghosts[0].moves[i].turns = 1; 
    }

    // Ghost 1
    board->board[2 * board->width + 4].content = 'M'; // Monster
    board->ghosts[1].pos_x = 4;
    board->ghosts[1].pos_y = 2;
    board->ghosts[1].passo = 1;
    board->ghosts[1].waiting = 1;
    board->ghosts[1].current_move = 0;
    board->ghosts[1].n_moves = 1;
    board->ghosts[1].moves[0].command = 'R'; // Random
    board->ghosts[1].moves[0].turns = 1; 
    
    return 0;
}

/**
 * Loads a ghost from file, by reading the file and parsing its content from the buffer
 */
int load_ghost_file(board_t* board, const char* filepath, int ghost_index) {
    char* buffer[MAX_MOVES + 3];
    *buffer = read_file((char*)filepath, board, -1);
    board->ghosts[ghost_index].n_moves = board->cnt_moves;
    //buffer[0] -> passo
    //buffer[1] -> pos_x
    //buffer[2] -> pos_y
    //buffer[3..] -> moves
    board->ghosts[ghost_index].passo = atoi(buffer[0]);
    board->ghosts[ghost_index].pos_x = atoi(buffer[1]);
    board->ghosts[ghost_index].pos_y = atoi(buffer[2]);
    board->board[board->ghosts[ghost_index].pos_y * board->width + board->ghosts[ghost_index].pos_x].content = 'M';
    board->ghosts[ghost_index].waiting = board->ghosts[ghost_index].passo;
    board->ghosts[ghost_index].current_move = 0;
    for (int i = 0; i < board->ghosts[ghost_index].n_moves; i++) {
        board->ghosts[ghost_index].moves[i].command = buffer[3 + i][0];
        board->ghosts[ghost_index].moves[i].turns = 1; // Default 1 turn per move
    }
    load_ghost(board);
    return 0;
}

// Static Loading
int load_level(board_t *board, int points) {
    board->height = 5;
    board->width = 10;
    board->tempo = 10;

    board->n_ghosts = 2;
    board->n_pacmans = 1;

    board->board = calloc(board->width * board->height, sizeof(board_pos_t));
    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    sprintf(board->level_name, "Static Level");

    for (int i = 0; i < board->height; i++) {
        for (int j = 0; j < board->width; j++) {
            if (i == 0 || j == 0 || j == (board->width - 1)) {
                board->board[i * board->width + j].content = 'W'; 
            }
            else if (i == 4 && j == 8) {
                board->board[i * board->width + j].content = ' ';
                board->board[i * board->width + j].has_portal = 1;
            }
            else {
                board->board[i * board->width + j].content = ' ';
                board->board[i * board->width + j].has_dot = 1;
            }
        }
    }

    load_ghost(board);
    load_pacman(board, points);

    return 0;
}

/**
 * Loads a level from file
 */
int load_level_file(board_t *board, const char *filepath, int max_files_to_load, int points) {
    //files_to_load will contain all files to load: pacman first (if it exists), then ghosts
    char *files_to_load[max_files_to_load];
    *files_to_load = read_file((char*)filepath, board, max_files_to_load);
    // If there is a pacman file, load it
    int mon_index = 0;
    if (board->n_pacmans > 0) {
        mon_index = 1;
        char pacman_path[256];
        snprintf(pacman_path, sizeof(pacman_path), "%s/%s", filepath, files_to_load[0]);
        load_pacman_file(board, pacman_path);
    } else {
        load_pacman(board, points); // Load default pacman
    }
    // Load ghost files
    board->n_ghosts = max_files_to_load - mon_index;
    for (int i = mon_index; i < max_files_to_load; i++) {
        char ghost_path[256];
        snprintf(ghost_path, sizeof(ghost_path), "%s/%s", filepath, files_to_load[i]);
        load_ghost_file(board, ghost_path, i - mon_index);
    }
    sprintf(board->level_name, "Loaded Level from %s", basename((char*)filepath));
    return 0;
}

/**
 * Reads and sends the lines of a file to a parser
 * The file can be .lvl (if max_files_to_load!=-1) or .m/.p (else)
 *
 * Returns the .m/.p files to open (if .lvl) or a creature buffer (else)
 */
char* read_file(char* filepath, board_t *board, int max_files_to_load) {
    int size = max_files_to_load;
    if (size == -1) {
        size = MAX_MOVES + 3; // Creature buffer size
    }
    char* return_vector[size];
    
    int index = 0;
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
    
    file_content[total_read] = '\0';
    
    // Process the file content to build the level
    board->current_board_line = 0;
    board->cnt_moves = 0;
    char *line = strtok(file_content, "\n");
    
    while (line != NULL) {
        if (line[0] != '#' && line[0] != '\0') {
            if (max_files_to_load == -1) {
                return_vector[index++] = parse_line_creature(board, line); //return_vector is crature buffer
            } else {
                return_vector[index++] = parse_line(board, line, max_files_to_load); //return_vector is files to load
            }
        }
        line = strtok(NULL, "\n");
    }
    
    free(file_content);
    close(fd);
    return *return_vector;
}

/**
 * Parses a line from .lvl file
 *
 * Returns the .m/.p files to open
 */
char* parse_line(board_t *board, char *line, int max_files_to_load) {
    char *files_to_load[max_files_to_load];
    int index = 0;
    int n_pacmans = 0;
    int n_ghosts = 0;
    if (strncmp(line, "DIM", 3) == 0) {
        sscanf(line + 4, "%d %d", &board->height, &board->width);
    } else if (strncmp(line, "TEMPO", 5) == 0) {
        sscanf(line + 6, "%d", &board->tempo);
    } else if (strncmp(line, "PAC", 3) == 0) {
        char *token = strtok(line + 4, " ");
        index = 0;
        while (token != NULL && index) { //There should be just 1 pacman
            files_to_load[index++] = strdup(token);
            token = strtok(NULL, " ");
        }
        board->n_pacmans = n_pacmans = index; // Update the number of pacmans
        strcpy(board->pacman_file,files_to_load[index-1]); // Store pacman file
    } else if (strncmp(line, "MON", 3) == 0) {
        char *token = strtok(line + 4, " ");
        int ghosts_index = 0;
        while (token != NULL) {
            files_to_load[index++] = strdup(token); // Store each filename
            token = strtok(NULL, " ");
            n_ghosts++;
            strcpy(board->ghosts_files[ghosts_index++], files_to_load[index-1]); // Store ghost file
        }
        board->n_ghosts = n_ghosts;
    } else {
        // Assume it's board content
        if (!board->board) {
            board->board = malloc(board->width * board->height * sizeof(board_pos_t));
        }
        // Fill the board row by row
        for (int i = 0; i < board->width; i++) {
            char current_char = line[board->current_board_line++];
            board->board[(board->height - 1) * board->width + i].content = current_char;
            if (current_char == 'X') {
                board->board[(board->height - 1) * board->width + i].content = 'W';
            } else if (current_char == 'o') {
                board->board[(board->height - 1) * board->width + i].has_dot = 1;
                board->board[(board->height - 1) * board->width + i].content = ' ';
            } else if (current_char == '@') {
                board->board[(board->height - 1) * board->width + i].has_portal = 1;
                board->board[(board->height - 1) * board->width + i].content = ' ';
            }
        }
    }
    return *files_to_load;
}

/**
 * Parses a line from .m/.p file
 *
 * Returns a creature buffer with the parsed data
 */
char* parse_line_creature(board_t *board, char *line) {
    char *buffer[MAX_MOVES + 3]; // line buffer
    int index = 0;
    if (strncmp(line, "PASSO", 5) == 0) {
        char passo_str[32];
        sscanf(line + 6, "%s", passo_str);
        buffer[index++] = strdup(passo_str);
    } else if (strncmp(line, "POS", 3) == 0) {
        char x_str[32], y_str[32];
        sscanf(line + 4, "%s %s", x_str, y_str);
        buffer[index++] = strdup(x_str);
        buffer[index++] = strdup(y_str);
    } else {
        // Assume it's a move or T
        board->cnt_moves++;
        char *token = strtok(line, " ");
        while (token != NULL) {
            buffer[index++] = strdup(token);
            token = strtok(NULL, " ");
        }
    }
    return *buffer;
}

void unload_level(board_t * board) {
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    fclose(debugfile);
}

void debug(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}


