/*
Simple Texas Hold'em (德州扑克) - C++ single-file demo
- 功能：发牌、转牌、摊牌，评估牌力，支持最多 6 名玩家，命令行交互（AI 玩家采用随机/简单策略）
- 适合作为简历项目的起点，可扩展为模拟/AI/网络对战

构建：
  g++ -std=c++17 -O2 -o holdem texas_holdem_cpp_project.cpp
运行：
  ./holdem

*/

#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <iostream>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
using namespace std;
namespace py = pybind11;

// ---------- 基本类型 ----------
enum Suit { CLUBS=0, DIAMONDS=1, HEARTS=2, SPADES=3 };
const string SUIT_CHARS = "CDHS";
const string RANK_STR = "23456789TJQKA"; // index 0 -> '2'

struct Card {
    int rank; // 2..14 (A=14)
    int suit; // 0..3
    Card(){}
    Card(int r,int s):rank(r),suit(s){}
    string str() const {
        char r = RANK_STR[rank-2];
        char s = SUIT_CHARS[suit];
        return string()+r+s;
    }
};

struct Deck {
    vector<Card> cards;
    Deck(){ reset(); }
    void reset(){
        cards.clear();
        for(int s=0;s<4;s++) for(int r=2;r<=14;r++) cards.emplace_back(r,s);
    }
    void shuffle(){
        static random_device rd;
        static mt19937 g(rd());
        std::shuffle(cards.begin(), cards.end(), g);
    }
    Card deal(){
        Card c = cards.back(); cards.pop_back(); return c;
    }
};

struct State {
    vector<int> holeCards;      // 手牌
    vector<int> boardCards;     // 公共牌
    int pot;
    int currentBet;
    int chips;
    vector<int> otherChips;     // 其他玩家筹码
    vector<int> currentBets;
    vector<bool> folded;        // 其他玩家是否弃牌
};

enum ActionType { FOLD=0, CALL=1, RAISE=2 };

struct Action {
    ActionType type;
    int raiseAmount; // 仅 RAISE 时有效
};

// ---------- 手牌评估 ----------
// 返回 (主等级, tiebreaker vector) 越大越好
// 主等级：0=高牌,...,8=straight flush

pair<int, vector<int>> evaluate7(const vector<Card>& seven){
    // seven.size() == 7
    // 1) 统计
    vector<int> cntRank(15,0); // 2..14
    vector<int> cntSuit(4,0);
    for(auto &c: seven){ cntRank[c.rank]++; cntSuit[c.suit]++; }
    // collect ranks present
    vector<int> ranks;
    for(int r=14;r>=2;r--) if(cntRank[r]) ranks.push_back(r);

    // Check flush
    int flushSuit=-1;
    for(int s=0;s<4;s++) if(cntSuit[s]>=5) flushSuit=s;
    vector<int> flushRanks;
    if(flushSuit!=-1){
        for(auto &c: seven) if(c.suit==flushSuit) flushRanks.push_back(c.rank);
        sort(flushRanks.begin(), flushRanks.end(), greater<int>());
        flushRanks.erase(unique(flushRanks.begin(), flushRanks.end()), flushRanks.end());
    }

    // Check straight (and straight flush)
    auto find_best_straight = [&](const vector<int>& rank_list)->int{
        if(rank_list.empty()) return -1;
        // create boolean array for ranks
        vector<int> has(15,0);
        for(int r: rank_list) has[r]=1;
        // Ace-low straight support: treat A as 1 for 5-high straight
        if(has[14]) has[1]=1;
        for(int top=14;top>=5;top--){
            bool ok=true;
            for(int k=0;k<5;k++) if(!has[top-k]){ ok=false; break; }
            if(ok) return top; // top rank of straight
        }
        return -1;
    };

    int straight_flush_top = -1;
    if(flushSuit!=-1){
        straight_flush_top = find_best_straight(flushRanks);
    }

    int straight_top = find_best_straight(ranks);

    // Count multiples
    vector<pair<int,int>> groups; // (count, rank)
    for(int r=14;r>=2;r--) if(cntRank[r]) groups.emplace_back(cntRank[r], r);
    sort(groups.begin(), groups.end(), [](auto &a, auto &b){ if(a.first!=b.first) return a.first>b.first; return a.second>b.second; });

    // Four of a kind
    if(groups[0].first==4){
        int fourRank = groups[0].second;
        int kicker=-1;
        for(int r=14;r>=2;r--) if(r!=fourRank && cntRank[r]){ kicker=r; break; }
        return {7, {fourRank, kicker}}; // 主等级 7 -> 四条
    }
    // Full house: three+pair (or two threes -> use second three as pair)
    if(groups[0].first==3){
        int threeRank = groups[0].second;
        int pairRank = -1;
        // find pair or second three
        for(size_t i=1;i<groups.size();i++) if(groups[i].first>=2){ pairRank = groups[i].second; break; }
        if(pairRank!=-1){
            return {6, {threeRank, pairRank}}; // 主等级 6 -> 葫芦
        }
    }
    // Flush
    if(flushSuit!=-1){
        vector<int> top5(flushRanks.begin(), flushRanks.begin()+min((size_t)5, flushRanks.size()));
        return {5, top5};
    }
    // Straight
    if(straight_top!=-1){
        return {4, {straight_top}}; // 主等级4 -> 顺子
    }
    // Three of a kind
    if(groups[0].first==3){
        int threeRank = groups[0].second;
        vector<int> kickers;
        for(int r=14;r>=2 && kickers.size()<2;r--) if(r!=threeRank && cntRank[r]) kickers.push_back(r);
        vector<int> v = {threeRank}; v.insert(v.end(), kickers.begin(), kickers.end());
        return {3, v};
    }
    // Two pair
    if(groups[0].first==2 && groups.size()>=2 && groups[1].first==2){
        int highPair = groups[0].second;
        int lowPair = groups[1].second;
        int kicker=-1;
        for(int r=14;r>=2;r--) if(r!=highPair && r!=lowPair && cntRank[r]){ kicker=r; break; }
        return {2, {highPair, lowPair, kicker}};
    }
    // One pair
    if(groups[0].first==2){
        int pairRank = groups[0].second;
        vector<int> kickers;
        for(int r=14;r>=2 && kickers.size()<3;r--) if(r!=pairRank && cntRank[r]) kickers.push_back(r);
        vector<int> v = {pairRank}; v.insert(v.end(), kickers.begin(), kickers.end());
        return {1, v};
    }
    // High card
    vector<int> top5;
    for(int r=14;r>=2 && top5.size()<5;r--) if(cntRank[r]) top5.push_back(r);
    return {0, top5};
}

// 比较两个评估结果
bool betterHand(const pair<int, vector<int>>& a, const pair<int, vector<int>>& b){
    if(a.first!=b.first) return a.first>b.first;
    for(size_t i=0;i<max(a.second.size(), b.second.size()); i++){
        int av = i<a.second.size()?a.second[i]:-1;
        int bv = i<b.second.size()?b.second[i]:-1;
        if(av!=bv) return av>bv;
    }
    return false; // equal
}

string handRankName(int rank){
    static vector<string> names = {"High Card","One Pair","Two Pair","Three of a Kind","Straight","Flush","Full House","Four of a Kind","Straight Flush"};
    return names[rank];
}

// ---------- 玩家与游戏逻辑 ----------
struct Player {
    string name;
    bool isAI;
    int chips;
    bool folded;
    int currentBet; // 新增：本轮已下注金额
    vector<Card> hole; // 2 cards
    Player(string n="Player", bool ai=false, int c=1000):name(n),isAI(ai),chips(c),folded(false),currentBet(0){}
};

struct Game {
    Deck deck;
    vector<Player> players;
    vector<Card> board; // community cards
    int dealerPos;
    int smallBlind, bigBlind;
    int pot;         // 当前底池
    int currentBet;  // 当前轮最大下注

    Game(int numPlayers=2){
        dealerPos=0; smallBlind=25; bigBlind=50;pot = 0;currentBet = 0;
        players.clear();
        for(int i=0;i<numPlayers;i++){
            string n = "AI_" + to_string(i);
            bool ai = true;
            players.emplace_back(n, ai, 1000);
        }
    }
    void resetPot() {
        pot = 0;
}

    void newRound(){
    deck.reset(); deck.shuffle();
    board.clear();
    for(auto &p: players){ 
        p.hole.clear(); 
        p.folded=false; 
        p.currentBet = 0; // 新增：重置本轮下注
    }
    // deal hole cards
    for(int r=0;r<2;r++) for(auto &p: players) p.hole.push_back(deck.deal());
}


    void dealFlop(){ deck.deal(); // burn
        board.push_back(deck.deal()); board.push_back(deck.deal()); board.push_back(deck.deal()); }
    void dealTurn(){ deck.deal(); board.push_back(deck.deal()); }
    void dealRiver(){ deck.deal(); board.push_back(deck.deal()); }

    void showState(bool showAll=false){
        cout<<"Board: "; for(auto &c: board) cout<<c.str()<<" "; cout<<"\n";
        for(size_t i=0;i<players.size();i++){
            auto &p = players[i];
            cout<<i<<":"<<p.name<<" (chips="<<p.chips<<") ";
            if(!p.folded)   cout<<"hole="<<p.hole[0].str()<<p.hole[1].str();
            else cout<<"(folded)";
            cout<<"\n";
        }
    }

    void showdown(){
    pair<int, vector<int>> bestEval = {-1,{}}; 
    vector<int> winners;

    for(size_t i=0;i<players.size();i++){
        if(players[i].folded) continue;
        vector<Card> seven = players[i].hole;
        seven.insert(seven.end(), board.begin(), board.end());
        auto ev = evaluate7(seven);
        cout<<players[i].name<<" -> "<<handRankName(ev.first)<<" (";
        for(auto t: ev.second) cout<<t<<" "; cout<<")\n";
        if(bestEval.first==-1 || betterHand(ev, bestEval)){
            bestEval = ev; winners.clear(); winners.push_back(i);
        } else if(!betterHand(ev, bestEval) && !betterHand(bestEval, ev)){
            winners.push_back(i);
        }
    }

    cout<<"Winner(s): ";
    for(int idx: winners) cout<<players[idx].name<<" "; cout<<"\n";

    // --- 把底池平分给赢家 ---
    int share = pot / winners.size();
    for(int idx: winners){
        players[idx].chips += share;
    }
    pot = 0; // 清空底池
}



    void bettingRound(int startingPlayer) {
    int numPlayers = players.size();
    currentBet = 0;
    for(auto &p: players) p.currentBet = 0;

    int idx = startingPlayer;
    int numActive = checkActivePlayers();
    int lastRaiser = -1;

    while(true){
        auto &p = players[idx];
        if(!p.folded){
            // 不再自己决定，而是交给Python
            cout << "Waiting for Python action for " << p.name << endl;
            break;  //  注意：这里不做动作，Python 用 step() 来推进
        }
        idx = (idx+1) % numPlayers;
    }
}



    void playOneHand(){
    newRound();
    cout << "--- Pre-flop ---\n";

    // 小盲、大盲位置
    int sbPos = (dealerPos+1) % players.size();
    int bbPos = (dealerPos+2) % players.size();

    // 扣盲注
    players[sbPos].chips -= smallBlind; players[sbPos].currentBet = smallBlind;
    players[bbPos].chips -= bigBlind; players[bbPos].currentBet = bigBlind;

    pot = smallBlind + bigBlind;
    currentBet = bigBlind;

    // 从大盲下一位开始下注
    bettingRound((bbPos+1) % players.size());

    dealFlop(); 
    cout << "--- Flop ---\n"; 
    showState(false); 
    bettingRound((dealerPos+1) % players.size());
    if(checkActivePlayers()<=1){ revealAndShowdown(); return; }

    dealTurn(); 
    cout << "--- Turn ---\n"; 
    showState(false); 
    bettingRound((dealerPos+1) % players.size());
    if(checkActivePlayers()<=1){ revealAndShowdown(); return; }

    dealRiver(); 
    cout << "--- River ---\n"; 
    showState(false); 
    bettingRound((dealerPos+1) % players.size());

    revealAndShowdown();

    // 轮转 dealer 位置，下一局使用
    dealerPos = (dealerPos + 1) % players.size();
}


    int checkActivePlayers(){ int cnt=0; for(auto &p:players) if(!p.folded) cnt++; return cnt; }

    void revealAndShowdown(){
        cout<<"--- Showdown ---\n";
        showState(true);
        showdown();
    }

    State getState(int idx){
    State s;
    auto &p = players[idx];

    for(auto &c: p.hole) s.holeCards.push_back(c.rank*10 + c.suit);
    for(auto &c: board) s.boardCards.push_back(c.rank*10 + c.suit);

    s.pot = pot;
    s.currentBet = currentBet;
    s.chips = p.chips;

    for(size_t i=0;i<players.size();i++){
        s.otherChips.push_back(players[i].chips);
        s.currentBets.push_back(players[i].currentBet);
        s.folded.push_back(players[i].folded);  // <-- 改名为 otherFolded
    }
    return s;
}

    void resetBets() {
        for(auto &p : players) p.currentBet = 0;
}
    void win(int idx,int pot) {
        players[idx].chips +=pot; 
}

    void applyAction(int idx, Action action){
    auto &p = players[idx];
    if(p.folded) return;

    if(action.type == FOLD){
        p.folded = true;
    } else if(action.type == CALL){
        int callAmt = currentBet - p.currentBet;
        callAmt = min(callAmt, p.chips);
        p.chips -= callAmt;
        p.currentBet += callAmt;
        pot += callAmt;
    } else if(action.type == RAISE){
        int raiseAmt = action.raiseAmount;
        if(raiseAmt > p.chips + p.currentBet) raiseAmt = p.chips + p.currentBet;
        pot += raiseAmt - p.currentBet;
        p.chips -= (raiseAmt - p.currentBet);
        p.currentBet = raiseAmt;
        currentBet = raiseAmt;
    }
}

    bool isDone(){ return checkActivePlayers()<=1 || board.size()==5; }

    vector<int> getReward(){
    vector<int> reward(players.size(), 0);
    if(checkActivePlayers()<=1){
        for(size_t i=0;i<players.size();i++) if(!players[i].folded) reward[i]=pot;
    } else if(board.size()==5){
        // 简单摊牌
        pair<int, vector<int>> bestEval = {-1,{}};
        vector<int> winners;
        for(size_t i=0;i<players.size();i++){
            if(players[i].folded) continue;
            vector<Card> seven = players[i].hole;
            seven.insert(seven.end(), board.begin(), board.end());
            auto ev = evaluate7(seven);
            if(bestEval.first==-1 || betterHand(ev, bestEval)){
                bestEval = ev; winners.clear(); winners.push_back(i);
            } else if(!betterHand(ev, bestEval) && !betterHand(bestEval, ev)){
                winners.push_back(i);
            }
        }
        int share = pot / winners.size();
        for(int idx: winners) reward[idx] = share;
    }
    return reward;
}



};

PYBIND11_MODULE(poker_env, m){
    py::class_<Card>(m, "Card")
    .def(py::init<int, int>())
    .def_readwrite("rank", &Card::rank)
    .def_readwrite("suit", &Card::suit)
    .def("str", &Card::str);

    py::class_<State>(m,"State")
        .def_readwrite("holeCards",&State::holeCards)
        .def_readwrite("boardCards",&State::boardCards)
        .def_readwrite("pot",&State::pot)
        .def_readwrite("currentBet",&State::currentBet)
        .def_readwrite("chips",&State::chips)
        .def_readwrite("otherChips",&State::otherChips)
        .def_readonly("currentBets", &State::currentBets)
        .def_readwrite("folded",&State::folded);

    py::enum_<ActionType>(m,"ActionType")
        .value("FOLD",FOLD)
        .value("CALL",CALL)
        .value("RAISE",RAISE)
        .export_values();

    py::class_<Action>(m,"Action")
        .def(py::init<>())
        .def_readwrite("type",&Action::type)
        .def_readwrite("raiseAmount",&Action::raiseAmount);

    py::class_<Game>(m,"Game")
    .def(py::init<int>())
    .def_readwrite("dealerPos", &Game::dealerPos)   // <-- 添加这一行
    .def("getState",&Game::getState)
    .def("applyAction",&Game::applyAction)
    .def("isDone",&Game::isDone)
    .def("getReward",&Game::getReward)
    .def("newRound",&Game::newRound)
    .def("dealFlop",&Game::dealFlop)
    .def("dealTurn",&Game::dealTurn)
    .def("dealRiver",&Game::dealRiver)
    .def("playOneHand",&Game::playOneHand)
    .def("resetBets",&Game::resetBets)
    .def("win",&Game::win)
    .def("resetPot",&Game::resetPot);

    m.def("betterHand",&betterHand);
    m.def("evaluate7",&evaluate7);
}
