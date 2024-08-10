
#include <ctype.h>
#include <curses.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "ascii.c"

#define WALL '$'
#define FOOD '*'
#define POWERUP 'O'
#define TELEPORT '/'
#define MAX_WIDTH 50
#define MAX_HEIGHT 50
#define MAX_GHOSTS 4
#define startx 8
#define starty 4

#define FRAME_RATE 100000


char** g_save_map;
int g_release_ghost = 0;
int g_pac_frame = -1;
int g_food_count = 0;
int g_level = 0;

int g_lvl_times[8] = {10,20,7,20,5,20,5,10000}; // time
int g_lvl_modes[8] = {0,1,0,1,0,1,0,1}; // mode
int g_lvl_stage = 0;

int NUM_GHOSTS=0;

WINDOW* win;

typedef struct{ int row, col; } vec;

typedef struct{
    char** map;
    int width, height, area;
    int score, lives, level;
    vec teleports[2];
} state;

typedef struct{
    vec scatter_square;
    /*
        0: Scatter
        1: Chase
        2: Frightened
        3: Dead (come back after frightened frenzy)
    */
    int mode;
    vec pos, last_pos, spawn;
    int opt_count;
    vec options[4];
} ghost;

typedef struct{
    vec pos, dir, old_dir, spawn;
    int food_eaten;
} pacman;


void run_game_for_time(state* game, pacman* pac, ghost* ghosts, int time);
void update_display(state* game, pacman* pac, ghost* ghosts);
void init_ghost_colors();


void clear_mem(state* game, ghost* ghosts){
    int i;
    for(i=0;i<game->height;++i){
        free(game->map[i]);
        free(g_save_map[i]);
    }
    free(game->map);
    free(g_save_map);

    free(ghosts);
}

// 3 rows, 6 columns. x,y is the center
void add_square(int y, int x, char c){
    int i;
    int offsets[18][2] = {
        {-1, -2}, {-1,-1}, {-1,0}, {-1,1}, {-1, 2}, {-1, 3},
        {0, -2}, {0,-1}, {0,0}, {0,1}, {0, 2}, {0, 3},
        {1, -2}, {1,-1}, {1,0}, {1,1}, {1, 2}, {1,3}
    };
    attron(COLOR_PAIR(5));
    for(i=0;i<18;++i)
        mvaddch(y+offsets[i][0], x+offsets[i][1], c);
    attroff(COLOR_PAIR(5));
}

void _calc_ghost_opts(state* game, ghost* ghost){
    int i, nr, nc;
    vec np;
    int opts[4][2] = {
                {-1,0},
        {0,-1},         {0,1},
                { 1,0}
    };
    for(i=0, ghost->opt_count=0;i<4;++i){
        nr = ghost->pos.row + opts[i][0];
        nc = ghost->pos.col + opts[i][1];

        if(nr == ghost->last_pos.row && nc == ghost->last_pos.col) continue;

        if(nr<0||nr>=game->height||nc<0||nc>=game->width) continue;

        if(game->map[nr][nc] == WALL || game->map[nr][nc] == '-') continue;

        np.row = nr;
        np.col = nc;
        ghost->options[ghost->opt_count] = np;
        ++ghost->opt_count;
    }
}

// return index of the best opt in ghost->options
int _calc_ghost_best(vec target, ghost* ghost){
    if(target.row == -1) return rand()%ghost->opt_count;

    int i, best_opt =0;
    float d1,d2, dist, closest_dist=1000.0f;

    for(i=0;i<ghost->opt_count;++i){
        d1 = abs(ghost->options[i].row - target.row);
        d2 = abs(ghost->options[i].col - target.col);
        dist = sqrtf(powf(d1,2) + powf(d2,2));
        if(dist < closest_dist){
            closest_dist = dist;
            best_opt = i;
        }
    }
    return best_opt;
}

void reset_level(state* game, pacman* pac, ghost* ghosts){
    int i,j;

    for(i=0;i<game->height;++i){
        for(j=0;j<game->width;++j){
            if(g_save_map[i][j] == ' '){
                game->map[i][j] = FOOD;
            }else if(g_save_map[i][j] == '.'){
                game->map[i][j] = POWERUP;
            }
        }
    }

    pac->pos = pac->spawn;
    pac->dir.row = 0; // y
    pac->dir.col = -1; // x
    pac->old_dir= pac->dir;
    pac->food_eaten = 0;

    for(i=0;i<NUM_GHOSTS;++i){
        ghosts[i].mode = 0;
        ghosts[i].pos = ghosts[i].last_pos = ghosts[i].spawn;
    }
    init_ghost_colors();
    g_release_ghost = 0;
    g_lvl_stage = 0;

    update_display(game, pac, ghosts);
    wrefresh(win);

    usleep(FRAME_RATE*35);
}

void setup_game(state* game, pacman* pac, ghost* ghosts){

    int i, j, k=0, z=0, tele_ind = 0;
    for(i=0;i<game->height;++i){
        for(j=0;j<game->width;++j){
            if(game->map[i][j] == 'p'){
                // pacman
                pac->pos.row = i;
                pac->pos.col = j;
                pac->spawn = pac->pos;
                pac->dir.row = 0; // y
                pac->dir.col = -1; // x
                pac->old_dir= pac->dir;
                pac->food_eaten = 0;
                game->map[i][j] = ' ';
            }else if(game->map[i][j] == 'g'){
                if(NUM_GHOSTS+1>MAX_GHOSTS){ continue; }
                ++NUM_GHOSTS;

                // ghosts
                ghosts[k].mode = 0; // start in scatter
                ghosts[k].last_pos.row = ghosts[k].last_pos.col =  -1; // has no last position
                ghosts[k].pos.row = i;
                ghosts[k].pos.col = j;
                ghosts[k].spawn = ghosts[k].pos;
                _calc_ghost_opts(game, ghosts+k);

                ++k;
                game->map[i][j] = '-';

            }else if(game->map[i][j] == ' '){
                ++g_food_count;
                game->map[i][j] = FOOD;
            }else if(game->map[i][j] == '.'){
                ++g_food_count;
                game->map[i][j] = POWERUP;
            }else if(game->map[i][j] == 'W'){
                game->map[i][j] = WALL;
            }else if(game->map[i][j] == '/'){
                game->map[i][j] = TELEPORT;
                game->teleports[tele_ind].row = i;
                game->teleports[tele_ind].col = j;
                ++tele_ind;
            }else if(isdigit(game->map[i][j])){
                // a number for where the ghosts will target on scatter mode
                if(z >= MAX_GHOSTS){ continue; }
                ghosts[z].scatter_square.row = i;
                ghosts[z].scatter_square.col = j;
                ++z;
                game->map[i][j] = WALL; // NOTE, THESE TARGET SQUARES SHOULD BE A PART OF THE WALL
            }
        }
    }
}


void print_pacman(int y, int x, pacman* pac){
    // change based on direction
    int i, num_lines = 3;
    const char** p_frame;
    // center the 3x6 pac drawing
    y-=1; x-=2;

    if (pac->dir.col == 1) {
        p_frame = right_frames[g_pac_frame % 3];
    } else if (pac->dir.col == -1) {
        p_frame = left_frames[g_pac_frame % 3];
    } else if (pac->dir.row == 1) {
        p_frame = down_frames[g_pac_frame % 3];
    } else if (pac->dir.row == -1) {
        p_frame = up_frames[g_pac_frame % 3];
        num_lines = 4;
    }

    attron(COLOR_PAIR(6));
    for(i=0;i<3;++i){
        mvprintw(y + i, x, "%s", p_frame[i]);
    }
    attroff(COLOR_PAIR(6));
}

void print_ghost(int y, int x, ghost* g) {
    int i;
    int rd = g->pos.row - g->last_pos.row;
    int cd = g->pos.col - g->last_pos.col;

    // center the 3x6 ghost drawing
    y-=1; x-=2;

    const char** g_frame;
    if (cd > 0) {
        g_frame = (g->mode == 2) ? right_frightened : right_normal;
    } else if (cd < 0) {
        g_frame = (g->mode == 2) ? left_frightened : left_normal;
    }else{
        // up and down
        g_frame = (g->mode == 2) ? right_frightened : right_normal;
    }

    for(i=0;i<3;++i){
        mvprintw(y + i, x, "%s", g_frame[i]);
    }
}

void update_display(state* game, pacman* pac, ghost* ghosts){
    int i, j;
    for(i=0;i<game->height;++i){
        for(j=0;j<game->width;++j){
            // W for wall
            if(game->map[i][j] == WALL){
                add_square(starty+3*i,startx+6*j, WALL);
            }else if(game->map[i][j] != '-'){
                // - is where food cannot exist
                mvaddch(starty+3*i, startx+6*j, game->map[i][j]);
            }
        }
    }

    for(i=0;i<NUM_GHOSTS;++i){
        attron(COLOR_PAIR(i+1));
        print_ghost(starty+3*ghosts[i].pos.row, startx+6*ghosts[i].pos.col, ghosts+i);
        attroff(COLOR_PAIR(i+1));
    }

    print_pacman(starty+3*pac->pos.row, startx+6*pac->pos.col, pac);

    mvprintw(starty-2, startx-2, "Score: %d", game->score);
    mvprintw(starty-2, startx+game->width*2, "Level: %d", game->level);
    mvprintw(starty-2, startx+game->width*5, "Lives: %d", game->lives);
}

void run_death(state* game, pacman* pac, ghost* ghosts){

    int i;
    char death_animation[10] = {'V', 'v', '_', '.', '+', '*', 'X', '*', '+', '.'};

    for(i=0;i<10;++i){
        attron(COLOR_PAIR(6));
        mvaddch(starty+3*pac->pos.row, startx+6*pac->pos.col, death_animation[i]);
        attroff(COLOR_PAIR(6));
        wrefresh(win);
        usleep(FRAME_RATE);
    }

    // RESET PACMAN AND GHOSTS
    pac->pos = pac->spawn;
    pac->dir.row = 0; // y
    pac->dir.col = -1; // x
    pac->old_dir= pac->dir;


    for(i=0;i<NUM_GHOSTS;++i){
        ghosts[i].mode = 0;
        ghosts[i].pos = ghosts[i].last_pos = ghosts[i].spawn;
    }
    g_release_ghost = 0;
    g_lvl_stage = 0;

    werase(win);
    update_display(game, pac, ghosts);
    wrefresh(win);

    usleep(FRAME_RATE*25);
}

void init_ghost_colors(){
    init_pair(1, COLOR_RED, COLOR_BLACK); // Ghost 1
    init_pair(2, COLOR_MAGENTA, COLOR_BLACK); // Ghost 2
    init_pair(3, COLOR_CYAN, COLOR_BLACK); // Ghost 3
    init_pair(4, COLOR_GREEN, COLOR_BLACK); // Ghost 4
}

void scared_ghosts(){
    init_pair(1, COLOR_BLUE, COLOR_BLACK); // Ghost 1
    init_pair(2, COLOR_BLUE, COLOR_BLACK); // Ghost 2
    init_pair(3, COLOR_BLUE, COLOR_BLACK); // Ghost 3
    init_pair(4, COLOR_BLUE, COLOR_BLACK); // Ghost 4
}

void game_over_scrn(state* game, ghost* ghosts){
    werase(win);

    mvprintw(LINES/2 - 2, COLS/2 - 6, "GAME OVER");
    mvprintw(LINES/2, COLS/2 - 7, "Score: %d", game->score);
    mvprintw(LINES/2 + 2, COLS/2 - 7, "Level: %d", game->level);

    wrefresh(win);
    usleep(FRAME_RATE*50);


    clear_mem(game, ghosts);
    endwin();
    exit(EXIT_SUCCESS);
}

void update_frame(state* game, pacman* pac, ghost* ghosts, int sent_input_flag){
    int i, invalid_pacmove_flag, pac_row, pac_col, best_opt;
    vec opt, target_vec;
    int flag_win=0;


    // UPDATE SCORE AND STATE BASED ON WHERE PACMAN IS NOW

    switch(game->map[pac->pos.row][pac->pos.col]){
        case FOOD:
            game->score+=100;
            //check for a win
            game->map[pac->pos.row][pac->pos.col] = ' '; // OLD POSITION IS EATEN
            flag_win = ++pac->food_eaten == g_food_count;
            break;
        case POWERUP:
            flag_win = ++pac->food_eaten == g_food_count;
            game->map[pac->pos.row][pac->pos.col] = ' '; // OLD POSITION IS EATEN
            if(!flag_win){
                for(i=0;i<NUM_GHOSTS;++i) ghosts[i].mode = 2;
                scared_ghosts();
                run_game_for_time(game, pac, ghosts, 6);
                init_ghost_colors();
                for(i=0;i<NUM_GHOSTS;++i) ghosts[i].mode = 0;
            }
            break;
    }

    if(flag_win){
        // WIN
        ++game->level;
        usleep(FRAME_RATE*25);
        reset_level(game, pac, ghosts);
        return;
    }

    // FIND NEW MOVEMENT OF PACMAN AND GHOSTS

    pac_row = pac->pos.row + pac->dir.row;
    pac_col = pac->pos.col + pac->dir.col;

    invalid_pacmove_flag = 0;
    if(pac_row<0 || pac_row>=game->height ||
            game->map[pac_row][pac_col]==WALL ||
            game->map[pac_row][pac_col]=='-'){
        pac_row = pac->pos.row;
        invalid_pacmove_flag = 1;
    }
    if(pac_col<0 || pac_col>=game->width ||
            game->map[pac_row][pac_col]==WALL ||
            game->map[pac_row][pac_col]=='-'){
        pac_col = pac->pos.col;
        invalid_pacmove_flag = 1;
    }

    // Keep moving in same direction if you try to run into a wall up or down
    if( invalid_pacmove_flag && sent_input_flag && (pac->dir.col != pac->old_dir.col ||
                                    pac->dir.row != pac->old_dir.row)){
        pac->dir = pac->old_dir;
        update_frame(game, pac, ghosts, 0);
        return;
    }

    for(i=0;i<g_release_ghost;++i){
        // DEAD FROM PACMAN (DONT MOVE UNITL OUT OF THIS MODE)
        if(ghosts[i].mode == 3) continue;

        _calc_ghost_opts(game, ghosts+i);

        if(ghosts[i].mode == 0){
            target_vec = ghosts[i].scatter_square;
        }else if(ghosts[i].mode == 1){
            target_vec = pac->pos;
        }else{
            // Frightened, move randomly without target square
            target_vec.row = target_vec.col = -1;
        }
        // find best opt
        best_opt = _calc_ghost_best(target_vec, ghosts+i);
        opt = ghosts[i].options[best_opt];

        if( opt.row == pac_row && opt.col == pac_col||
                /* check if they crossed paths head on */
            (opt.row==pac->pos.row&&opt.col==pac->pos.col&&
             pac_row==ghosts[i].pos.row&&pac_col==ghosts[i].pos.col)){

            // CHECK IF PACMAN EATS THIS GHOST
            if(ghosts[i].mode == 2){
                game->score += 500;
                ghosts[i].mode = 3; // Ghost is dead now
                ghosts[i].pos = ghosts[i].last_pos = ghosts[i].spawn;
            }else{
                if(--game->lives == 0){
                    // TODO: GAME OVER SCREEN
                    game_over_scrn(game, ghosts);
                }
                // Show that the ghost collided with pacman
                ghosts[i].last_pos = ghosts[i].pos;
                ghosts[i].pos = opt;
                werase(win);
                update_display(game, pac, ghosts);
                wrefresh(win);
                usleep(FRAME_RATE*3);

                run_death(game, pac, ghosts);
                return;
            }
        }else{
            ghosts[i].last_pos = ghosts[i].pos;
            ghosts[i].pos = opt;

            if(game->teleports[1].row != -1){
                if (opt.row == game->teleports[0].row && opt.col == game->teleports[0].col) {
                    ghosts[i].pos.row = game->teleports[1].row;
                    ghosts[i].pos.col = game->teleports[1].col;
                } else if (opt.row == game->teleports[1].row && opt.col == game->teleports[1].col) {
                    ghosts[i].pos.row = game->teleports[0].row;
                    ghosts[i].pos.col = game->teleports[0].col;
                }
            }
        }
    }

    // UPDATE POSITION OF PACMAN
    pac->pos.row = pac_row;
    pac->pos.col = pac_col;

    // make sure the teleport is a valid location
    if(game->teleports[1].row != -1){
        if (pac_row == game->teleports[0].row && pac_col == game->teleports[0].col) {
            pac->pos.row = game->teleports[1].row;
            pac->pos.col = game->teleports[1].col;
        } else if (pac_row == game->teleports[1].row && pac_col == game->teleports[1].col) {
            pac->pos.row = game->teleports[0].row;
            pac->pos.col = game->teleports[0].col;
        }
    }
}

void run_game_for_time(state* game, pacman* pac, ghost* ghosts, int time){
    int i=0, diff;

    struct timespec begin, end; 
    clock_gettime(CLOCK_REALTIME, &begin);

    for(;;++i){

        clock_gettime(CLOCK_REALTIME, &end);
        diff = end.tv_sec - begin.tv_sec;
        if(diff >= time) break;

        if(g_release_ghost < NUM_GHOSTS && i > 20){
            ++g_release_ghost;
            i=0;
        }
        // process input
        int ch = getch();
        int sent_input = 0;
        if(ch == KEY_LEFT){
            sent_input = 1;
            pac->old_dir = pac->dir;
            pac->dir.col = -1;
            pac->dir.row = 0;
        }else if(ch == KEY_RIGHT){
            sent_input = 1;
            pac->old_dir = pac->dir;
            pac->dir.col = 1;
            pac->dir.row = 0;
        }else if(ch == KEY_UP){
            sent_input = 1;
            pac->old_dir = pac->dir;
            pac->dir.col = 0;
            pac->dir.row = -1;
        }else if(ch == KEY_DOWN){
            sent_input = 1;
            pac->old_dir = pac->dir;
            pac->dir.col = 0;
            pac->dir.row = 1;
        }else if(ch == 'q'){
            clear_mem(game, ghosts);
            endwin();
            exit(EXIT_SUCCESS);
        }
        update_frame(game, pac, ghosts, sent_input);
        ++g_pac_frame;
        if(g_pac_frame>=100) g_pac_frame = 0;

        werase(win);
        update_display(game, pac, ghosts);
        wrefresh(win);

        usleep(FRAME_RATE);
    }
}




int main(int argc, char** argv){
    if(argc < 2){
        fprintf(stderr, "Usage: ./a.out <map_file>\n");
        exit(EXIT_FAILURE);
    }


    srand(time(NULL));
    int i, j, width=0, height=0, area=0;
    char buf[50];
    char** map = malloc(MAX_HEIGHT * sizeof(char*));
    for(i=0;i<MAX_HEIGHT;++i) map[i] = malloc(MAX_WIDTH * sizeof(char));


    g_save_map = malloc(MAX_HEIGHT * sizeof(char*));
    for(i=0;i<MAX_HEIGHT;++i) g_save_map[i] = malloc(MAX_WIDTH * sizeof(char));

    FILE* f = fopen(argv[1], "r");

    while(fgets(buf, sizeof(buf), f)){
        for(i=0;i<50 && buf[i]!='\0';++i){
            map[height][i] = buf[i];
            g_save_map[height][i] = buf[i];
        }
        if(!width) width = i;
        ++height;
    }
    area = width*height;

    state game = { map, width, height, area, 0, 3, 1};
    game.teleports[0].row = game.teleports[0].col = game.teleports[1].row = game.teleports[1].col = -1;

    pacman pac;
    ghost* ghosts = malloc(MAX_GHOSTS * sizeof(ghost));

    setup_game(&game, &pac, ghosts);

    win = initscr();
    noecho();
    cbreak();
    nodelay(win,1);
    curs_set(0);
    keypad(win, TRUE);

    if (has_colors() == FALSE) {
        clear_mem(&game, ghosts);
        endwin();
        printf("Your terminal does not support color\n");
        exit(1);
    }

    start_color();
    init_ghost_colors();
    init_pair(5, COLOR_BLUE, COLOR_BLACK); // Wall color
    init_pair(6, COLOR_YELLOW, COLOR_BLACK); // Pacman




    for(;g_lvl_stage<8;++g_lvl_stage){
        for(j=0;j<NUM_GHOSTS;++j) ghosts[j].mode = g_lvl_modes[g_lvl_stage];
        run_game_for_time(&game, &pac, ghosts, g_lvl_times[g_lvl_stage]);
    }

    clear_mem(&game, ghosts);
    endwin();
    return 0;
}
