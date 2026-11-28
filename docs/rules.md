# Feast Mode Rules

These rules define correct behavior. Unit tests are written from this file. If code and
this file disagree, the code is wrong (or this file gets updated first, on purpose).

## Players and rooms

- A room has a 4-letter uppercase code (letters only, no `I` or `O` to avoid confusion).
- A room holds up to 10 players. Normal mode needs 4+ to start; test mode (server flag
  `--test-mode`, for testing only) needs 2+.
- Names are 1–16 characters after trimming whitespace, and unique within a room
  (case-insensitive).
- The player who creates the room is the host, and is host again whenever they are
  connected (reconnecting gives it straight back).
- If the host disconnects, they stay host for **20 seconds** so a locked phone doesn't
  lose it (deadlines keep running meanwhile). After that, host passes to the connected
  player who joined earliest, until the creator comes back.
- Players can only join while the room is in the lobby. A disconnected player can rejoin
  at any time with their rejoin token (see Disconnects).

## Match flow

1. **Lobby.** Players join with the room code. The host starts the match.
2. **Mystery:** 3 rounds.
3. **Would You Rather Extreme:** 3 prompts.
4. **Around the Table:** one round per player (skipped when the server runs with
   `--no-around-the-table`, see Game 3).
5. **Awards.** Bonus awards from all the games, revealed one at a time, +75 each (see
   Awards). They can change who wins.
6. **Final leaderboard.** The highest score is champion. Tied top scores are co-champions.

Scores start at 0 and carry across every round and every game.

## Phase advancement

- Phases where players act (vote, guess, choose, pick) end when **every connected player
  who is still due an action has acted**, or when the deadline passes, whichever comes
  first.
- **Vote phase only:** a player who disconnected less than 20 seconds ago still counts as
  due. Otherwise a vote stuck waiting on one missing player would show everyone who the
  impostor is (see Disconnects).
- The discuss phase has nothing to submit but still has a deadline.
- Reveal phases have no deadline. The host taps **Next** to advance.
- The host can also tap **Next** during any phase to end it early (fallback if a timer
  misbehaves). Missing actions count as "did not act".

Deadlines: discuss 100 s, vote 45 s, guess 30 s, choose 30 s, Around the Table question
choice 20 s, Around the Table pick 45 s.

## Game 1: Mystery (word impostor)

Each round:

1. **Setup.** The server picks an unused word pair (main word, impostor word) and picks one
   random connected player as the impostor. No player is the impostor twice in a match
   unless every player has already been the impostor once. (The impostor of a void round
   counts as having had their turn.)
2. **Words.** Each player privately receives their word. **The impostor is told they are
   the impostor** and receives the impostor word. Non-impostors receive the main word and
   are told they are not the impostor.
   - **Word stays on screen.** Each player's word (and whether they are the impostor)
     stays visible on their own screen for the entire round, through discuss, vote, and
     guess, until the reveal.
   - **Starts blurred.** The word appears blurred when it arrives. The player taps to show
     it and taps again to blur it, as many times as they like, so neighbors can't glance at
     their phone. This is purely on the player's own screen: no message is sent to the
     server.
   - **After a rejoin**, the word comes back too (the server resends `YOUR_WORD`), blurred
     again.
3. **Discuss phase (clues + discussion).** Everything happens in person. Players take turns
   saying a clue about their word out loud, then discuss. Ends at the 100 s deadline, or
   when the host taps Next.
   - **Steal.** At any point during discussion, the impostor may guess the main word
     (once). The guess box is inside their word card, so it only shows while they have
     the card open. The round ends immediately and goes to the reveal:
     - **Right:** the impostor steals the round.
     - **Wrong:** the impostor has outed themselves and counts as caught. No final guess.
4. **Vote phase.** Each player secretly votes once, for either another player in the round
   or **Skip**. You cannot vote for yourself. Voting for a disconnected player is allowed.
5. **Vote result.** Votes are tallied with Skip counted like a candidate.
   - **Skip has the most votes, ties for the most, or two or more players tie for the
     most, or zero votes cast:** nobody is caught. The impostor escapes.
   - **One player has the most votes and is the impostor:** the impostor is caught.
   - **One player has the most votes and is not the impostor:** the impostor escapes.
   - **Escaped:** straight to the reveal. No guess.
6. **Final guess (hail mary), only if caught.** The caught impostor gets one last guess at
   the main word. Everyone else waits. A guess (here or in a steal) is correct if it
   matches the main word after trimming and ignoring case.
7. **Reveal.** Everyone sees all votes (if there was a vote), who was accused, who the
   impostor was, both words, the impostor's guess (if any) and whether it was a steal,
   whether it was correct, and points earned.

### Mystery scoring

| Outcome | Impostor | Each non-impostor in the round |
|---|---|---|
| Steal: right guess during discussion | +300 | 0 |
| Wrong guess during discussion (outed, counts as caught) | 0 | +100 |
| Escapes the vote (including all ties and Skip wins) | +200 | 0 |
| Caught, then final guess right | +100 | +100 |
| Caught, final guess wrong or none | 0 | +100 |

"In the round" means the players dealt a word at setup, connected or not.

## Game 2: Would You Rather Extreme

Each prompt:

1. **Prompt.** The server picks an unused prompt with two extreme options, A and B
   (e.g. "Eat only cranberry sauce for a year" vs. "Never eat pie again").
2. **Choose phase.** Each player secretly picks the option they would choose (A or B),
   at most once.
3. **Reveal.** Everyone sees who chose what, the A/B counts, which side won, and points
   earned. Arguing happens in person.

The majority is whichever option more players chose. If the counts are equal, there is no
majority.

### Would You Rather scoring

| Outcome | Points |
|---|---|
| You picked the majority option | +100 |
| You are the only player who chose your option (and at least 3 players chose) | +50 ("lone wolf") |
| No majority (even split) | no majority points for anyone |

Only players who submitted count toward the majority.

## Game 3: Around the Table

A prediction game for getting to know each other. Each round one player is the
**subject**, and everyone else predicts how the subject will answer a question about
themselves. The point is less the score than finding out things you didn't know and
talking about them.

Everyone knows how it works: the subject chooses what to answer, nobody has to answer
anything they'd rather not, and nothing is shown to anyone (host included) before the
reveal.

Each round:

1. **Subject.** The server picks a random connected player who hasn't been the subject yet
   this match. Everyone sees who it is.
2. **Question choice (20 s), subject only.** The subject privately sees a question from
   `docs/around-the-table-questions.md` with its four options (A to D) and taps
   **Continue** to use it, or **Skip** to see another one, up to **2 skips** per turn.
   Everyone else sees "Alex is picking a question…". Skipped questions are never shown to
   anyone else. If the 20 s run out (or the host taps Next), the question on screen is used.
3. **Pick phase (45 s).** The chosen question and its options appear on every phone and
   the big screen, with the subject's name, e.g. "Alex: You have a completely free
   Saturday. What are you doing?" At the same time:
   - the **subject** privately picks their real answer, and
   - **every other player** privately picks what they think the subject chose, or taps
     **Pass** (no prediction, no penalty).
   Each player picks once and can't change it. Nobody sees anyone's pick, the subject's
   included, until the reveal.
4. **Reveal.** Everyone sees the subject's answer, every player's prediction (or pass), who
   was right, and points. The subject can explain their pick out loud, especially if it
   surprised everyone.
5. **Next subject.** The host taps Next. The game continues until every player has been the
   subject once.

### Around the Table scoring

Your design's 2-point / 3-point values, scaled to match the other games (×50):

| Outcome | Points |
|---|---|
| You predicted the subject's answer | +100 |
| Perfect round: at least 2 players predicted and **every** prediction was right | +150 more to each of them (+250 total) |
| Wrong prediction, or Pass | 0 |
| You are the subject | 0 (so there's no reason to answer anything but honestly) |
| The subject didn't answer in time | Nobody scores this round; the reveal says so |

### Rounds and turns

- **One round per player** in the room when the match starts, so everyone gets a turn.
- If the subject disconnects while choosing, the question on screen is used when the 20 s
  run out.
- A player who is **disconnected when the next subject is picked** is passed over for now
  and picked in a later round if they're back. Players still away after everyone else has
  had a turn are skipped, and the game ends.
- The pick phase ends early only once **every connected player has picked or passed and
  the subject has answered**. It never ends early without the subject's answer; only the
  deadline or the host's Next does that.

### Turning it off

The server flag `--no-around-the-table` leaves Game 3 out of the match entirely: the match
is Mystery, then Would You Rather, then the leaderboard, exactly as before Game 3 existed
(and no Around the Table awards). Use it if anything about Game 3 misbehaves on the
night.

### Later (not for Saturday)

From the design, saved for after game night because each needs new kinds of screens:

- **The Group Knows You:** the subject writes a short answer, everyone writes a guess, and
  the subject awards points to the closest guesses.
- **"Who in the group…?" rounds:** everyone picks which player fits a description.
- **Custom questions** added by the host.

## Awards (end of match)

Mario Party style: after the last game, before the final leaderboard, bonus awards are
revealed **one at a time** on every screen. Each award is worth **+75** to each winner,
added to their score as it's revealed, so the lead can change right at the end.

| Game | Award | Goes to |
|---|---|---|
| Mystery | 🔎 **Detective** | Most votes for the actual impostor (in rounds you weren't the impostor) |
| Mystery | 🕵️ **Master of Disguise** | Most rounds won as the impostor (escaped the vote or stole) |
| Would You Rather | 🐑 **Crowd Pleaser** | Most picks on the majority side |
| Would You Rather | 🐺 **Lone Wolf** | Most times as the only one on your side (3+ answering) |
| Around the Table | 🔮 **Mind Reader** | Most correct predictions |
| Around the Table | 🤝 **Closest Friend** | Best prediction accuracy (correct ÷ predictions made, at least 2 made) |
| Around the Table | 🌀 **Plot Twist** | The subject whose answer the smallest share of predictors got right |
| Around the Table | 📖 **Open Book** | The subject whose answer the largest share of predictors got right |

- **Ties share the award**, and each winner gets the +75.
- **Nobody qualifies, no award:** you need at least 1 of the thing counted (e.g. 1 vote
  for the impostor). Plot Twist and Open Book count only subjects who answered and had at
  least one prediction, and are left out if every such subject had the same share.
- Awards are revealed in the order of the table. The host taps Next for each one; after the
  last, the final leaderboard. If nobody qualifies for any award, it goes straight to the
  leaderboard.
- **Ending early:** if the host taps End match, there are no awards; it goes straight to
  the final leaderboard.
- Around the Table awards only exist when Game 3 was played.

## Disconnects

The game continues without disconnected players and adjusts:

- A disconnected player is marked `connected: false` in room state. They keep their score.
- Phases stop waiting for disconnected players (see Phase advancement).
- **Mystery, impostor disconnects before the vote result:** they have **20 seconds to
  reconnect**, and if they do, the round carries on. If they don't (or the vote ends while
  they are still away), the round is void: no points, and the reveal shows both words.
  When the host taps Next, **the same round is played again** with new words and a new
  impostor, so a void round does not use up one of the 3 rounds.
- **Mystery, impostor disconnects during the guess phase:** counts as no guess. Points
  from the vote result still apply.
- **Mystery, non-impostor disconnects:** they stay in the round. Votes for them still
  count, and they still earn points if the impostor is caught.
- **Would You Rather:** a disconnected player's submitted choice still counts and they can
  still earn points.
- **Around the Table:** a disconnected player's pick still counts and still earns points.
  If the subject disconnects before answering, the round waits for them until the
  deadline (or the host's Next); if they never answer, nobody scores that round.
- **Rejoin:** a player who reconnects with their rejoin token gets their seat, score, and
  (if the round is still running) their private word back. They take part from the
  current phase on.
- **Too few players:** if fewer than 3 players (2 in test mode) are connected when a round
  would start,
  the match pauses in place until enough reconnect, or the host ends the match early
  (final leaderboard is shown).
- **Everyone gone:** a room with no connected players for 10 minutes is deleted.

## Out of scope for Saturday

Pumpkin Bowling (stretch), Fake Family Facts, in-game chat, avatars, sound, accounts,
persistence across server restarts, any host-only or hidden view of players' picks.
