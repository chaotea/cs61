#include "board.hh"


// pong_board(w, h) constructor
//    Construct a new `w x h` pong board with all empty cells.
pong_board::pong_board(int w, int h)
    : width(w), height(h), cells(w * h, pong_cell()) {
    obstacle_cell.type = cell_obstacle;
    column_mutex = new std::mutex[w+2];
}


// pong_board destructor
pong_board::~pong_board() {
    for (auto w : this->warps) {
        delete w;
    }
    delete[] column_mutex;
}


// pong_ball::move()
//    Move this ball once.
//
//    Returns 1 if the ball successfully moved to an empty cell.
//    Returns -1 if the ball fell off the board.
//    Returns 0 otherwise. (The caller should not delay the next move if
//    this function returns 0.)
//
//    This function is complex because it must consider obstacles, collisions,
//    holes, and sticky cells. You should preserve its current logic while
//    adding sufficient synchronization to make it thread-safe.
int pong_ball::move() {
    pong_cell& ccur = board.cell(this->x, this->y);

    // if stopped (i.e. sticky), block until the ball is dislodged
    board.column_mutex[this->x+1].lock();
    while (this->stopped && !this->unwarp) {
        this->stopped_cv.wait(board.column_mutex[this->x+1]);
    }
    board.column_mutex[this->x+1].unlock();

    // if the ball is ready to unwarp, unwarp it
    if (this->unwarp) {
        pong_cell& cdest = this->board.cell(this->wx, this->wy);
        this->board.column_mutex[this->wx+1].lock();
        while (cdest.ball) {
            this->board.column_mutex[this->wx+1].unlock();
            this->board.column_mutex[this->wx+1].lock();
        }
        assert(!cdest.ball);
        cdest.ball = this;
        this->x = this->wx;
        this->y = this->wy;
        this->stopped = false;
        this->unwarp = false;
        this->board.column_mutex[this->wx+1].unlock();
        return 0;
    }

    // otherwise, ball is on board in this cell
    // lock the current, left, and right columns that the ball is on
    assert(ccur.ball == this);
    std::scoped_lock guard(board.column_mutex[this->x], board.column_mutex[this->x+1], board.column_mutex[this->x+2]);

    // change direction on hitting an obstacle
    pong_cell& cx = this->board.cell(this->x + this->dx, this->y);
    if (cx.type >= cell_obstacle) {
        cx.hit_obstacle();
        this->dx = -this->dx;
    }

    pong_cell& cy = this->board.cell(this->x, this->y + this->dy);
    if (cy.type >= cell_obstacle) {
        cy.hit_obstacle();
        this->dy = -this->dy;
    }

    // check next cell
    pong_cell& cnext = this->board.cell(this->x + this->dx,
                                        this->y + this->dy);
    if (cnext.ball) {
        // collision: change both balls' directions without moving them
        if (cnext.ball->dx != this->dx) {
            cnext.ball->dx = this->dx;
            this->dx = -this->dx;
        }
        if (cnext.ball->dy != this->dy) {
            cnext.ball->dy = this->dy;
            this->dy = -this->dy;
        }
        cnext.ball->stopped = false;

        // increment collisions and notify the stopped ball
        this->board.collision_mutex.lock();
        ++this->board.ncollisions;
        this->board.collision_mutex.unlock();
        cnext.ball->stopped_cv.notify_all();
        return 0;
    } else if (cnext.type == cell_warp) {
        // warp: fall off board into warp tunnel
        ccur.ball = nullptr;
        this->stopped = true;
        cnext.warp->accept_ball(this);
        return 0;
    } else if (cnext.type == cell_trash) {
        // trash: kill ball
        ccur.ball = nullptr;
        return -1;
    } else if (cnext.type >= cell_obstacle) {
        // obstacle: reverse direction (but do not move)
        cnext.hit_obstacle();
        this->dx = -this->dx;
        this->dy = -this->dy;
        return 0;
    } else {
        // empty or sticky: move into it
        this->x += this->dx;
        this->y += this->dy;
        ccur.ball = nullptr;
        cnext.ball = this;
        if (cnext.type == cell_sticky) {
            // sticky: stay put until next collision
            this->dx = this->dy = 0;
            this->stopped = true;
        }
        return 1;
    }
}


// pong_cell::hit_obstacle()
//    Called from pong_ball::move when a ball hits an obstacle or paddle.

void pong_cell::hit_obstacle() {
    if (this->type == cell_obstacle
        && this->strength != 0
        && --this->strength == 0) {
        this->type = cell_empty;
    }
}


// pong_warp::accept_ball(b)
//    Called from pong_ball::move when ball `b` lands on one end of a warp
//    tunnel. Hands `b` off to that tunnel for further processing (see
//    `warp_thread` in `breakout61.cc`)
//
//    The handout code has several synchronization bugs, including that if
//    multiple balls enter a warp tunnel too close together, an assertion will
//    fail. (Instead, all balls should be accepted and then processed in
//    order.)

void pong_warp::accept_ball(pong_ball* b) {
    // push the new ball to the back of the queue
    this->queue_mutex.lock();
    this->q.push_back(b);
    this->queue_mutex.unlock();
    // wake up the warp thread
    this->cv.notify_all();
}
