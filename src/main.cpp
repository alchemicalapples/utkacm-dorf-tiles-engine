#include "Poco/Process.h"
#include "Poco/PipeStream.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>
#include <utility>
#include <vector>
#include <iterator>
#include <stdexcept>

using namespace std;

template <typename Iter, typename EIter>
ostream& _print_helper(Iter iter, EIter eiter) {
    while (iter != eiter) {
        if (*iter == '$') {
            ++iter;
            if (*iter == 'E') {
                cout << endl;
            } else if (*iter == '$') {
                cout << *iter;
            } else if (*iter == '?') {
                throw runtime_error("Not enough arguments!");
            } else {
                throw runtime_error("Invalid format specifier!");
            }
        } else if (*iter != '\0') {
            cout << *iter;
        }
        ++iter;
    }
    return cout;
}

template <typename Iter, typename EIter, typename T, typename... Ts>
ostream& _print_helper(Iter iter, EIter eiter, T const& head, Ts const&... tail) {
    while (iter != eiter) {
        if (*iter == '$') {
            ++iter;
            if (*iter == '?') {
                cout << head;
                ++iter;
                return _print_helper(iter, eiter, tail...);
            } else if (*iter == 'E') {
                cout << endl;
            } else if (*iter == '$') {
                cout << *iter;
            } else {
                throw runtime_error("Invalid format specifier!");
            }
        } else if (*iter != '\0') {
            cout << *iter;
        }
        ++iter;
    }
    throw runtime_error("Too many parameters!");
}

template <typename Str, typename... Ts>
ostream& print(Str const& fmt, Ts const&... ts) {
    return _print_helper(begin(fmt),end(fmt),ts...);
}

auto erase_remove_if = [](auto& container, auto&& pred) {
    return container.erase(
        remove_if(begin(container),end(container),pred),
        end(container));
};

struct ProcData {
    Poco::Pipe inPipe;
    Poco::Pipe outPipe;
    Poco::Pipe errPipe;

    Poco::PipeOutputStream writeIn;
    Poco::PipeInputStream readOut;
    Poco::PipeInputStream readErr;

    Poco::ProcessHandle handle;

    ProcData(string const& name)
        : inPipe(), outPipe(), errPipe()
        , writeIn(inPipe), readOut(outPipe), readErr(errPipe)
        , handle(Poco::Process::launch("sh", {"-c","'"+name+"'"}, &inPipe, &outPipe, &errPipe))
    {}
};

struct AI {
    int id;
    string name;
    unique_ptr<ProcData> proc;

    AI(int id, string n)
        : id(id)
        , name(move(n))
        , proc(make_unique<ProcData>(name))
    {}
};

struct Coord {
    int r;
    int c;
};

constexpr Coord bad_coord = {-1,-1};

ostream& operator<<(ostream& out, Coord const& c) {
    return out << c.r << " " << c.c;
}

Coord operator+(Coord const& a, Coord const& b) {
    return Coord{a.r+b.r,a.c+b.c};
}

struct AICoord {
    AI* ai;
    Coord coord;
    Coord move_to;
};

struct Board {
    using BoardElem = int;

    struct Row {
        vector<BoardElem>::iterator iter;
        Row(vector<BoardElem>::iterator iter) : iter(move(iter)) {}
        BoardElem& operator[](size_t i) const {
            return *(iter+i);
        }
    };

    int width;
    int height;
    vector<BoardElem> data;
    vector<AICoord> players;
    vector<AI*> dead_players;

    Board(int w, int h)
        : width(w)
        , height(h)
        , data(w*h, 0)
    {}

    Row operator[](size_t i) {
        return Row(begin(data)+width*i);
    }

    BoardElem& operator[](Coord const& c) {
        return (*this)[c.r][c.c];
    }

    bool in_bounds(Coord const& c) {
        return (
            c.r >= 0 && c.r < height &&
            c.c >= 0 && c.c < width);
    }
};

int main(int argc, char* argv[]) {

    if (argc!=3 && argc!=5) {
        cerr << "Usage: dorf ai1 ai2 [ai3 ai4]" << endl;
        return -1;
    }

    vector<unique_ptr<AI>> ais;
    ais.reserve(4);

    Board board (10,10);
    fill(begin(board.data),end(board.data),1);

    auto startlocs = array<Coord,4>{{
        {0,                          0},
        {board.height-1, board.width-1},
        {0,              board.width-1},
        {board.height-1,             0},
    }};

    for (int i=0; i<argc-1; ++i) {
        auto ptr = make_unique<AI>(i, argv[1+i]);
        print("Added \"$?\" as Player $?$E", argv[1+i], ptr->id);
        board.players.push_back({ptr.get(),startlocs[i]});
        ais.push_back(move(ptr));
    }

    for (auto& p : board.players) {
        auto& out = p.ai->proc->writeIn;
        out << p.ai->id << endl;
        out << board.height << " " << board.width << endl;
        for (int r=0; r<board.height; ++r) {
            for (int c=0; c<board.width; ++c) {
                if (c>0) {
                    out << " ";
                }
                out << board[r][c];
            }
            out << endl;
        }
    }

    string move_dir;

    unordered_map<string,Coord> move_dirs = {
        {"row+", { 1, 0}},
        {"row-", {-1, 0}},
        {"col+", { 0, 1}},
        {"col-", { 0,-1}},
    };

    int turn = 0;

    while (board.players.size() > 1) {
        ++turn;
        print("Turn $?$E", turn);
        for (auto& p : board.players) {
            auto& out = p.ai->proc->writeIn;
            for (auto& p2 : board.players) {
                out << p2.ai->id << " " << p2.coord << endl;
            }
            for (auto& ai : board.dead_players) {
                out << ai->id << " " << bad_coord << endl;
            }
        }
        for (auto& p : board.players) {
            auto& in = p.ai->proc->readOut;
            in >> move_dir;
            print("Player $? moves $?...$E", p.ai->id, move_dir);
            auto iter = move_dirs.find(move_dir);
            if (iter == end(move_dirs)) {
                print("Invalid move!$E");
                print("  Player $? has been destroyed!$E", p.ai->id);
                Poco::Process::requestTermination(p.ai->proc->handle.id());
                board.dead_players.emplace_back(exchange(p.ai,nullptr));
            } else {
                p.move_to = iter->second + p.coord;
            }
        }
        for (auto& p : board.players) {
            board[p.coord] -= 1;
            p.coord = p.move_to;
        }
        for (auto& p : board.players) {
            if (p.ai) {
                if (!board.in_bounds(p.coord) || board[p.coord] <= 0) {
                    print("Player $? died at {$?}$E", p.ai->id, p.coord);
                    Poco::Process::requestTermination(p.ai->proc->handle.id());
                    board.dead_players.emplace_back(exchange(p.ai,nullptr));
                }
            }
        }
        auto is_dead = [](auto& p){return !bool(p.ai);};
        erase_remove_if(board.players, is_dead);
    }

    print("GAME OVER$E");
    if (board.players.size() == 1) {
        auto& p = board.players[0];
        print("Winner: Player $? \"$?\"$E", p.ai->id, p.ai->name);
    } else {
        print("There were no survivors...$E");
    }

    for (auto& p : board.players) {
        Poco::Process::requestTermination(p.ai->proc->handle.id());
    }
}
