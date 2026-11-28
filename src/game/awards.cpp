#include "game/awards.h"

#include <algorithm>
#include <functional>
#include <optional>

namespace feast {

namespace {

int countOf(const std::map<std::string, int>& counts, const std::string& id) {
  auto it = counts.find(id);
  return it == counts.end() ? 0 : it->second;
}

// Everyone tied for the highest count, if that count is at least 1.
std::vector<std::string> mostOf(const std::map<std::string, int>& counts,
                                const std::vector<std::string>& playerIds) {
  int best = 0;
  for (const std::string& id : playerIds) best = std::max(best, countOf(counts, id));
  std::vector<std::string> winners;
  if (best < 1) return winners;
  for (const std::string& id : playerIds) {
    if (countOf(counts, id) == best) winners.push_back(id);
  }
  return winners;
}

// A share as a fraction, compared without rounding: a/b vs c/d via a*d vs c*b.
struct Share {
  int num;
  int den;
};
bool lessThan(Share a, Share b) { return a.num * b.den < b.num * a.den; }
bool sameAs(Share a, Share b) { return a.num * b.den == b.num * a.den; }

// Among players who qualify, those whose share is the best by `better`.
std::vector<std::string> bestShare(const std::vector<std::string>& playerIds,
                                   const std::function<std::optional<Share>(const std::string&)>& shareOf,
                                   bool wantHighest) {
  std::optional<Share> best;
  for (const std::string& id : playerIds) {
    std::optional<Share> s = shareOf(id);
    if (!s) continue;
    if (!best || (wantHighest ? lessThan(*best, *s) : lessThan(*s, *best))) best = s;
  }
  std::vector<std::string> winners;
  if (!best) return winners;
  for (const std::string& id : playerIds) {
    std::optional<Share> s = shareOf(id);
    if (s && sameAs(*s, *best)) winners.push_back(id);
  }
  return winners;
}

}  // namespace

std::vector<Award> computeAwards(const MatchStats& stats,
                                 const std::vector<std::string>& playerIds,
                                 bool withAroundTheTable) {
  std::vector<Award> awards;
  auto add = [&](const char* id, const char* title, const char* description,
                 std::vector<std::string> winners) {
    if (!winners.empty()) awards.push_back({id, title, description, std::move(winners)});
  };

  add("detective", "🔎 Detective", "Most votes for the actual impostor",
      mostOf(stats.impostorVotes, playerIds));
  add("master_of_disguise", "🕵️ Master of Disguise", "Most rounds won as the impostor",
      mostOf(stats.impostorWins, playerIds));
  add("crowd_pleaser", "🐑 Crowd Pleaser", "Most picks on the majority side",
      mostOf(stats.majorityPicks, playerIds));
  add("lone_wolf", "🐺 Lone Wolf", "Most times the only one on your side",
      mostOf(stats.loneWolfPicks, playerIds));
  if (!withAroundTheTable) return awards;

  add("mind_reader", "🔮 Mind Reader", "Most correct predictions",
      mostOf(stats.correctPredictions, playerIds));

  // Closest Friend: best correct / made, at least 2 made and at least 1 right.
  add("closest_friend", "🤝 Closest Friend", "Best prediction accuracy",
      bestShare(playerIds, [&](const std::string& id) -> std::optional<Share> {
        int made = countOf(stats.predictionsMade, id);
        int right = countOf(stats.correctPredictions, id);
        if (made < 2 || right < 1) return std::nullopt;
        return Share{right, made};
      }, true));

  // Plot Twist / Open Book: subjects who had at least one prediction. Left out
  // when every such subject had the same share (nobody stands out).
  auto subjectShare = [&](const std::string& id) -> std::optional<Share> {
    int made = countOf(stats.subjectPredictions, id);
    if (made < 1) return std::nullopt;
    return Share{countOf(stats.subjectCorrect, id), made};
  };
  std::vector<std::string> lowest = bestShare(playerIds, subjectShare, false);
  std::vector<std::string> highest = bestShare(playerIds, subjectShare, true);
  bool allSame = !lowest.empty() && subjectShare(lowest.front()) && subjectShare(highest.front()) &&
                 sameAs(*subjectShare(lowest.front()), *subjectShare(highest.front()));
  if (!allSame) {
    add("plot_twist", "🌀 Plot Twist", "Hardest to predict", lowest);
    add("open_book", "📖 Open Book", "Easiest to predict", highest);
  }
  return awards;
}

}  // namespace feast
