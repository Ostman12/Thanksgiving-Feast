# Feast Mode Rules

These rules define correct behavior. Unit tests are written from this file. If code and
this file disagree, the code is wrong (or this file gets updated first, on purpose).

## Players and rooms

- A room has a 4-letter uppercase code (letters only, no `I` or `O` to avoid confusion).
- A room holds 3–10 players. Normal mode needs 4+ to start; test mode (server flag
  `--test-mode`) needs 3+.
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
4. **Final leaderboard.** The highest score is champion. Tied top scores are co-champions.

Scores start at 0 and carry across every round and both games.

## Phase advancement

- Phases where players act (vote, guess, choose) end when **every connected player who is
  still due an action has acted**, or when the deadline passes, whichever comes first.
- **Vote phase only:** a player who disconnected less than 20 seconds ago still counts as
  due. Otherwise a vote stuck waiting on one missing player would show everyone who the
  impostor is (see Disconnects).
- The discuss phase has nothing to submit but still has a deadline.
- Reveal phases have no deadline. The host taps **Next** to advance.
- The host can also tap **Next** during any phase to end it early (fallback if a timer
  misbehaves). Missing actions count as "did not act".

Deadlines: discuss 100 s, vote 45 s, guess 30 s, choose 30 s.

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
2. **Choose phase.** Each player secretly submits two things at once, at most once:
   - **Choice:** which option they would pick (A or B).
   - **Prediction:** which option they think the majority of the room picked (A or B).
3. **Reveal.** Everyone sees who chose what, the A/B counts, which side won, each
   player's prediction, and points earned. Arguing happens in person.

The majority is whichever option more players chose. If the counts are equal, there is no
majority.

### Would You Rather scoring

| Outcome | Points |
|---|---|
| Your prediction matches the majority | +100 |
| You are the only player who chose your option (and at least 3 players chose) | +50 ("lone wolf") |
| No majority (even split) | no prediction points for anyone |

Only players who submitted count toward the majority.

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
- **Rejoin:** a player who reconnects with their rejoin token gets their seat, score, and
  (if the round is still running) their private word back. They take part from the
  current phase on.
- **Too few players:** if fewer than 3 players are connected when a round would start,
  the match pauses in place until enough reconnect, or the host ends the match early
  (final leaderboard is shown).
- **Everyone gone:** a room with no connected players for 10 minutes is deleted.

## Out of scope for Saturday

Pumpkin Bowling (stretch), Fake Family Facts, in-game chat, avatars, sound, accounts,
persistence across server restarts.
