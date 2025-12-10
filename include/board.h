#ifndef BOARD_H
#define BOARD_H

#define MAX_MOVES 20
#define MAX_LEVELS 20
#define MAX_FILENAME 256
#define MAX_GHOSTS 25

typedef enum {
    REACHED_PORTAL = 1,
    VALID_MOVE = 0,
    INVALID_MOVE = -1,
    DEAD_PACMAN = -2,
} move_t;

typedef struct {
    char command;
    int turns;
    int turns_left;
} command_t;

typedef struct {
    int pos_x, pos_y; 
    int alive; 
    int points; 
    int passo; 
    command_t moves[MAX_MOVES];
    int current_move;
    int n_moves; 
    int waiting;
} pacman_t;

typedef struct {
    int pos_x, pos_y; 
    int passo; 
    command_t moves[MAX_MOVES];
    int n_moves; 
    int current_move;
    int waiting;
    int charged;
} ghost_t;

typedef struct {
    char content;   
    int has_dot;    
    int has_portal; 
} board_pos_t;

typedef struct {
    int width, height;      
    board_pos_t* board;     
    int n_pacmans;          
    pacman_t* pacmans;      
    int n_ghosts;           
    ghost_t* ghosts;        
    char level_name[256];   
    char pacman_file[256];  
    char ghosts_files[MAX_GHOSTS][256]; 
    int tempo;              
    int current_board_line;      
    int board_line_count;       
    int cnt_moves;         
} board_t;

void sleep_ms(int milliseconds);
int move_pacman(board_t* board, int pacman_index, command_t* command);
int move_ghost(board_t* board, int ghost_index, command_t* command);
void kill_pacman(board_t* board, int pacman_index);
int load_pacman(board_t* board, int points);
int load_pacman_file(board_t* board, const char* filepath, int points);
int load_ghost(board_t* board);
int load_ghost_file(board_t* board, const char* filepath, int ghost_index);
int load_level(board_t* board, int accumulated_points);
int load_level_file(board_t *board, const char *filepath, int max_files_to_load, int points);

/* Changed return type to char** */
char** read_file(char* filepath, board_t *board, int max_files_to_load);

char* parse_line(board_t *board, char *line);
char* parse_line_creature(board_t *board, char *line);
void unload_level(board_t * board);
void open_debug_file(char *filename);
void close_debug_file();
void debug(const char * format, ...);
void print_board(board_t* board);

#endif