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
#include <pthread.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define DO_BACKUP 3
#define EXIT_PACMAN_DIED 5

// Estrutura para passar argumentos às threads
typedef struct {
    board_t *board;
    int id;
} thread_arg_t;

// Tarefa da Thread do Pacman
void* pacman_task(void* arg) {
    thread_arg_t *data = (thread_arg_t *)arg;
    board_t *board = data->board;
    int index = data->id;
    pacman_t * pac = &board->pacmans[index];

    // Loop enquanto o jogo corre e o Pacman está vivo
    while (board->game_running && pac->alive) {
        command_t cmd;
        cmd.turns = 1;

        if (pac->n_moves > 0) {
            // Movimento pré-definido (ficheiro)
            cmd = pac->moves[pac->current_move % pac->n_moves];
        } else {
            // Movimento do utilizador (controlado pela main thread via next_direction)
            char dir = pac->next_direction;
            if (dir != '\0') {
                cmd.command = dir;
                pac->next_direction = '\0'; // Consome o comando
            } else {
                cmd.command = '\0';
            }
        }

        if (cmd.command != '\0') {
            int result = move_pacman(board, index, &cmd);
            if (result == REACHED_PORTAL) {
                // Chegou ao portal, sinaliza paragem segura
                board->game_running = 0;
            } 
        }

        sleep_ms(board->tempo);
    }
    free(data);
    return NULL;
}

// Tarefa da Thread dos Fantasmas
void* ghost_task(void* arg) {
    thread_arg_t *data = (thread_arg_t*)arg;
    board_t *board = data->board;
    int index = data->id;
    ghost_t * ghost = &board->ghosts[index];

    while (board->game_running) {
        // Fantasmas movem-se autonomamente
        command_t *cmd = &ghost->moves[ghost->current_move % ghost->n_moves];
        move_ghost(board, index, cmd);
        sleep_ms(board->tempo);
    }
    free(data);
    return NULL;
} 

void screen_refresh(board_t * game_board, int mode) {
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
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

    // --- Carregamento de Níveis (Mantido igual) ---
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
    // ---------------------------------------------

    index_lp = 0;
    bool has_backup = false;

    // Loop Principal do Jogo (Níveis)
    while (!end_game) {
        // Carregar Nível
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

        int level_result = CONTINUE_PLAY;

        // Loop de "Vida" (Permite reiniciar threads após backup ou restore)
        while (true) {
            game_board.game_running = 1;
            
            // Variáveis locais para TIDs para não depender de alterações no board.h
            pthread_t pacman_tid;
            pthread_t ghost_tids[MAX_GHOSTS];

            // 1. Criar Threads
            if (game_board.n_pacmans > 0) {
                thread_arg_t *arg = malloc(sizeof(thread_arg_t));
                arg->board = &game_board;
                arg->id = 0;
                // Nota: Usamos a variável local, ignorando o campo tid da struct se ele não existir
                pthread_create(&pacman_tid, NULL, pacman_task, arg);
            }

            for (int i = 0; i < game_board.n_ghosts; i++) {
                thread_arg_t *arg = malloc(sizeof(thread_arg_t));
                arg->board = &game_board;
                arg->id = i;
                pthread_create(&ghost_tids[i], NULL, ghost_task, arg);
            }

            // 2. Loop de Input e Display (Main Thread)
            int exit_reason = CONTINUE_PLAY;
            
            while (game_board.game_running) {
                // Desenhar
                draw_board(&game_board, DRAW_MENU);
                refresh_screen();

                // Input
                char input = get_input();
                
                if (input == 'Q') {
                    game_board.game_running = 0;
                    exit_reason = QUIT_GAME;
                } 
                else if (input == 'G') {
                    if (!has_backup) { // Só permite 'G' se ainda não somos um backup
                        game_board.game_running = 0; // Pára o jogo para fazer fork
                        exit_reason = DO_BACKUP;
                    }
                } 
                else if (input != '\0' && game_board.n_pacmans > 0) {
                    // Passa o input para a thread do Pacman
                    game_board.pacmans[0].next_direction = input;
                }

                // Verificar condições de fim de jogo (detectadas pelas threads ou lógica)
                if (game_board.n_pacmans > 0 && !game_board.pacmans[0].alive) {
                    game_board.game_running = 0;
                    exit_reason = QUIT_GAME; // Morte é tratada como saída do loop
                }

                // Verificar vitória (se o pacman chegou ao portal, a thread mete game_running=0)
                if (game_board.n_pacmans > 0 && game_board.game_running == 0 && exit_reason == CONTINUE_PLAY) {
                     // Se parou sem ser Q ou G ou Morte, assumimos vitória (NEXT_LEVEL)
                     // (Idealmente teríamos uma flag de status, mas usamos a lógica existente)
                     if (game_board.pacmans[0].alive)
                        exit_reason = NEXT_LEVEL;
                     else
                        exit_reason = QUIT_GAME;
                }

                sleep_ms(game_board.tempo);
            }

            // 3. Juntar Threads (Obrigatório antes de fork ou sair)
            if (game_board.n_pacmans > 0) {
                pthread_join(pacman_tid, NULL);
            }
            for (int i = 0; i < game_board.n_ghosts; i++) {
                pthread_join(ghost_tids[i], NULL);
            }

            // 4. Processar Razão de Saída
            if (exit_reason == DO_BACKUP) {
                // LÓGICA DO FORK
                // As threads estão paradas. O estado é seguro para duplicar.
                pid_t pid = fork();

                if (pid < 0) {
                    perror("Fork failed");
                    exit_reason = CONTINUE_PLAY; // Tenta continuar se falhar
                } 
                else if (pid == 0) {
                    // --- PROCESSO FILHO ---
                    has_backup = true; // Marca que é backup
                    // Continua o loop "while(true)" -> recria threads e joga
                    continue; 
                } 
                else {
                    // --- PROCESSO PAI ---
                    int status;
                    wait(&status); // Bloqueia à espera do filho

                    if (WIFEXITED(status)) {
                        int exit_code = WEXITSTATUS(status);

                        if (exit_code == EXIT_PACMAN_DIED) {
                            // Filho morreu -> Restaurar
                            debug("Pacman morreu no filho. Restaurando estado...\n");
                            // Continua o loop "while(true)" -> recria threads COM O ESTADO ANTIGO (Pai)
                            screen_refresh(&game_board, DRAW_MENU);
                            continue; 
                        } 
                        else if (exit_code == NEXT_LEVEL) {
                            // Filho passou de nível -> Pai avança
                            level_result = NEXT_LEVEL;
                            break; // Sai do loop "while(true)" e vai carregar próximo nível
                        } 
                        else if (exit_code == QUIT_GAME) {
                            // Filho saiu -> Pai sai
                            level_result = QUIT_GAME;
                            break;
                        }
                    }
                    // Se algo estranho acontecer, restaura
                    continue;
                }
            }
            else {
                // Não é backup, é um fim normal (Win, Die, Quit)
                level_result = exit_reason;
                break; // Sai do loop de threads para processar o fim do nível
            }
        } // Fim while(true) - Vida/Threads

        // 5. Processar Resultado Final do Nível
        if (level_result == QUIT_GAME) {
            if (!game_board.pacmans[0].alive) {
                // Se morreu
                if (has_backup) {
                    exit(EXIT_PACMAN_DIED); // Filho avisa o pai
                }
                screen_refresh(&game_board, DRAW_GAME_OVER);
                sleep_ms(2000);
                end_game = true;
            } else {
                // Saída voluntária (Q)
                if (has_backup) {
                    exit(QUIT_GAME);
                }
                screen_refresh(&game_board, DRAW_GAME_OVER);
                sleep_ms(2000);
                end_game = true;
            }
        } 
        else if (level_result == NEXT_LEVEL) {
            accumulated_points = game_board.pacmans[0].points;
            screen_refresh(&game_board, DRAW_WIN);
            sleep_ms(2000);
            
            if (has_backup) {
                exit(NEXT_LEVEL); // Filho avisa o pai
            }
            // Pai continua para o próximo nível (loop externo)
        }

        unload_level(&game_board);
    } // Fim while(!end_game)

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