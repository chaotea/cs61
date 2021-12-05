#ifndef PONGBOARD_HH
#define PONGBOARD_HH
#include <cassert>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "helpers.hh"
struct pong_ball;
struct pong_warp;
int random_int(int min, int max);


enum pong_celltype {
    cell_empty = 0,
    cell_sticky = 1,
    cell_warp = 2,
    cell_trash = 3,
    cell_obstacle = 4,
    cell_paddle = 5
};


struct pong_cell {
    pong_celltype type = cell_empty;  // type of cell

    // Non-obstacles only:
    pong_ball* ball = nullptr;        // pointer to ball currently in cell
                                      // (if any)

    // Obstacles only:
    int strength = 0;                 // obstacle strength

    // Warp cells only:
    pong_warp* warp = nullptr;        // pointer to warp (if any)

    void hit_obstacle();
};


struct pong_board {
    int width;
    int height;
    std::vector<pong_cell> cells;     // `width * height`, row-major order
    std::vector<pong_warp*> warps;

    pong_cell obstacle_cell;          // represents off-board positions
    unsigned long ncollisions = 0;

    std::mutex* column_mutex;  // array of mutexes for each column (including the boundaries)
    std::mutex collision_mutex;  // mutex for collisions

    pong_board(int w, int h);
    ~pong_board();

    // boards can't be copied, moved, or assigned
    pong_board(const pong_board&) = delete;
    pong_board& operator=(const pong_board&) = delete;


    // cell(x, y)
    //    Return a reference to the cell at position `x, y`. If there is
    //    no such position, returns `obstacle_cell`, a cell containing an
    //    obstacle.
    pong_cell& cell(int x, int y) {
        if (x < 0 || x >= this->width || y < 0 || y >= this->height) {
            return this->obstacle_cell;
        } else {
            return this->cells[y * this->width + x];
        }
    }
};



struct pong_ball {
    pong_board& board;
    bool stopped = false;
    bool unwarp = false;  // if true, unwarp the ball in move()
    int x = 0;
    int y = 0;
    int dx = 1;
    int dy = 1;
    int wx = -1;  // the x position of the warp tunnel exit
    int wy = -1;  // the y position of the warp tunnel exit
    std::condition_variable_any stopped_cv;  // cv for stopped balls

    // pong_ball(board)
    //    Construct a new ball on `board`.
    pong_ball(pong_board& board_)
        : board(board_) {
    }

    // balls can't be copied, moved, or assigned
    pong_ball(const pong_ball&) = delete;
    pong_ball& operator=(const pong_ball&) = delete;


    // move()
    //    Move this ball once on its board. Returns 1 if the ball moved to an
    //    empty cell, -1 if it fell off the board, and 0 otherwise.
    int move();
};


struct pong_warp {
    pong_board& board;
    int x;
    int y;
    std::deque<pong_ball*> q;  // queue for storing balls that have entered the warp
    std::mutex queue_mutex;  // mutex for the queue
    std::condition_variable_any cv;  // cv for the queue

    pong_warp(pong_board& board_)
        : board(board_) {
    }

    // transfer a ball into this warp tunnel
    void accept_ball(pong_ball* b);
};

#endif
