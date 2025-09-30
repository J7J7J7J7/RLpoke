import random
import poker_env
from poker_env import evaluate7, betterHand

RANK_STR = "23456789TJQKA"
SUIT_CHARS = "CDHS"

def card_to_str(card_id):
    rank = card_id // 10
    suit = card_id % 10
    return RANK_STR[rank-2] + SUIT_CHARS[suit]

def print_board(board):
    print("Board:", " ".join(card_to_str(c) for c in board) if board else "[]")

def print_state(state, pid):
    hole = " ".join(card_to_str(c) for c in state.holeCards)
    print(f"Player {pid} hole cards: {hole}")
    #print_board(state.boardCards)
    print(f"Pot={state.pot}, CurrentBet={state.currentBet}, Chips={state.chips}")

def print_action(pid, act):
    if act.type == poker_env.ActionType.FOLD:
        print(f"Player {pid} folds")
    elif act.type == poker_env.ActionType.CALL:
        print(f"Player {pid} calls")
    elif act.type == poker_env.ActionType.RAISE:
        print(f"Player {pid} raises to {act.raiseAmount}")

def ai_policy(state):
    """简单随机策略"""
    act = poker_env.Action()
    r = random.random()
    if r < 0.1:
        act.type = poker_env.ActionType.FOLD
    elif r < 0.75:
        act.type = poker_env.ActionType.CALL
    else:
        act.type = poker_env.ActionType.RAISE
        act.raiseAmount = state.currentBet + 50
    return act

def convert_to_Cards(int_list):
    cards = []
    for c in int_list:
        rank = c // 10
        suit = c % 10
        cards.append(poker_env.Card(rank, suit))
    return cards

def settlePot(game, num_players):
    folded = [game.getState(pid).folded[pid] for pid in range(num_players)]
    print(folded)
    unfolded = 0
    unfoldeds = []
    poke=[]
    board = game.getState(0).boardCards
    pot = game.getState(0).pot
    for idx in range(num_players):
        if not folded[idx] :
            unfolded = unfolded + 1
            unfoldeds.append(idx)
            poke.append(game.getState(idx).holeCards + board)  
    
    print(poke)
    hand_evals = []
    for hand in poke:
        hand_ = convert_to_Cards(hand)
        hand_evals.append(evaluate7(hand_))
    best_eval = hand_evals[0]
    winners = [unfoldeds[0]]

    for i in range(1, len(hand_evals)):
        idx = unfoldeds[i]
        if betterHand(hand_evals[i], best_eval):
            best_eval = hand_evals[i]
            winners = [idx]  # <-- 发现更强手牌，重置 winners
        elif not betterHand(hand_evals[i], best_eval) and not betterHand(best_eval, hand_evals[i]):
            winners.append(idx)  # <-- 平手才加入 winners

    share = pot // len(winners)

    for w in winners:
        game.win(w, share)

def post_blinds(game, num_players, dealer_pos, small_blind=25, big_blind=50):
    """
    按庄家位置循环设置小盲和大盲
    dealer_pos: 当前庄家位置
    """
    sb_pos = (dealer_pos + 1) % num_players  # 小盲在庄家下家
    bb_pos = (dealer_pos + 2) % num_players  # 大盲在小盲下家

    # 发小盲
    sb_act = poker_env.Action()
    sb_act.type = poker_env.ActionType.RAISE
    sb_act.raiseAmount = small_blind
    game.applyAction(sb_pos, sb_act)

    # 发大盲
    bb_act = poker_env.Action()
    bb_act.type = poker_env.ActionType.RAISE
    bb_act.raiseAmount = big_blind
    game.applyAction(bb_pos, bb_act)

    print(f"Player {sb_pos} posts small blind: {small_blind}")
    print(f"Player {bb_pos} posts big blind: {big_blind}")
    return sb_pos, bb_pos

def betting_round_pre(game, num_players, dealer_pos):
    start_idx = (dealer_pos + 3) % num_players
    idx = start_idx
    finished = False
    """循环下注直到所有活跃玩家下注相等"""
    while not finished:
        finished = True
        for _ in range(num_players):
            state = game.getState(idx)
            folded = state.folded[idx]
            currentBet = state.currentBets[idx]
            # 所有玩家的下注和状态
            bets = game.getState(0).currentBets
            active = [not game.getState(pid).folded for pid in range(num_players)]
            max_bet = max([b for b, a in zip(bets, active) if not a], default=0)
            if not folded and currentBet < max_bet:
                act = ai_policy(state)
                game.applyAction(idx, act)
                print_action(idx, act)
                finished = False

            idx = (idx + 1) % num_players

def betting_round(game, num_players, dealer_pos):
    start_idx = (dealer_pos + 3) % num_players
    idx = start_idx
    signal = -1
    finished = False
    """循环下注直到所有活跃玩家下注相等"""
    while not finished:
        finished = True
        for _ in range(num_players):
            if idx == start_idx :
                signal = signal + 1
            state = game.getState(idx)
            folded = state.folded[idx]
            currentBet = state.currentBets[idx]
            # 所有玩家的下注和状态
            bets = game.getState(0).currentBets
            active = [not game.getState(pid).folded for pid in range(num_players)]
            max_bet = max([b for b, a in zip(bets, active) if not a], default=0)
            if not folded and currentBet < max_bet:
                act = ai_policy(state)
                game.applyAction(idx, act)
                print_action(idx, act)
                finished = False
            elif not folded and signal == 0 :
                act = ai_policy(state)
                game.applyAction(idx, act)
                print_action(idx, act)
                finished = False
            idx = (idx + 1) % num_players

def play_one_hand(game, dealer_pos, num_players=6, small_blind=25, big_blind=50):
    """
    game: 已经创建好的 poker_env.Game 对象，每局使用同一个对象保证筹码累积
    dealer_pos: 当前庄家位置
    """
    pre_chips = [game.getState(pid).chips for pid in range(num_players)]
    game.newRound()
    print("\n=== New Hand ===")

    # 打印每个玩家手牌
    for pid in range(num_players):
        print_state(game.getState(pid), pid)

    # 发盲注
    sb_pos, bb_pos = post_blinds(game, num_players, dealer_pos, small_blind, big_blind)

    # Pre-flop
    print("\n--- Pre-flop ---")
    betting_round_pre(game, num_players, dealer_pos)
    game.resetBets()
    for pid in range(num_players):
        print_state(game.getState(pid), pid)

    # Flop
    game.dealFlop()
    print("\n--- Flop ---")
    print_board(game.getState(0).boardCards)
    betting_round(game, num_players, dealer_pos)
    game.resetBets()

    for pid in range(num_players):
        print_state(game.getState(pid), pid)

    # Turn
    game.dealTurn()
    print("\n--- Turn ---")
    print_board(game.getState(0).boardCards)
    betting_round(game, num_players, dealer_pos)
    game.resetBets()

    for pid in range(num_players):
        print_state(game.getState(pid), pid)

    # River
    game.dealRiver()
    print("\n--- River ---")
    print_board(game.getState(0).boardCards)
    betting_round(game, num_players, dealer_pos)
    game.resetBets()
    for pid in range(num_players):
        print_state(game.getState(pid), pid)

    
    # 摊牌 & 计算每个玩家筹码变化
    settlePot(game,num_players)
    post_chips = [game.getState(pid).chips for pid in range(num_players)]
    rewards = [post - pre for post, pre in zip(post_chips, pre_chips)]

    print("\n--- Hand Result ---")
    for pid, r in enumerate(rewards):
        print(f"Player {pid} net change: {r}")

    return rewards


def play_games(num_players=6, num_hands=3):
    game = poker_env.Game(num_players)  # 外层创建，保证筹码累积

    dealer_pos = -1  # 初始庄家
    history = []

    for i in range(num_hands):
        dealer_pos = (dealer_pos + 1) % num_players  # 每局轮庄
        print(f"\n=== Hand {i+1} ===")
        rewards = play_one_hand(game, dealer_pos, num_players)
        history.append(rewards)
        game.resetPot()

    print("\n=== All Hands History ===")
    for i, r in enumerate(history):
        print(f"Hand {i+1}: {r}")

    # 最终每个玩家筹码
    print("\n=== Final Chips ===")
    for pid in range(num_players):
        print(f"Player {pid}: {game.getState(pid).chips}")

    return history


if __name__ == "__main__":
    history = play_games(num_players=6, num_hands=2)
    print("\n=== All Hands History ===")
    for i, r in enumerate(history):
        print(f"Hand {i+1}: {r}")
