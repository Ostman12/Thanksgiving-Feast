#include "server/protocol.h"

#include <type_traits>

namespace feast {

using nlohmann::json;

namespace {

json message(const char* type, json payload) {
  return json{{"type", type}, {"payload", std::move(payload)}};
}

// std::optional -> value or null.
template <class T>
json orNull(const std::optional<T>& value) {
  return value ? json(*value) : json(nullptr);
}

json optionToJson(const std::optional<char>& option) {
  return option ? json(std::string(1, *option)) : json(nullptr);
}

json pointsToJson(const std::vector<PointsDelta>& points) {
  json list = json::array();
  for (const PointsDelta& p : points) list.push_back({{"playerId", p.playerId}, {"delta", p.delta}});
  return list;
}

json toJson(const RoomStateEvent& e) {
  json players = json::array();
  for (const PublicPlayer& p : e.players) {
    players.push_back(
        {{"id", p.id}, {"name", p.name}, {"score", p.score}, {"connected", p.connected}});
  }
  return message("ROOM_STATE", {{"code", e.code},
                                {"hostId", e.hostId},
                                {"phase", phaseName(e.phase)},
                                {"testMode", e.testMode},
                                {"players", players}});
}

json toJson(const MatchStartedEvent& e) {
  // json::array is explicit: a bare {"mystery", "wyr"} would become an object.
  json games = json::array({"mystery", "wyr"});
  json rounds = {{"mystery", e.mysteryRounds}, {"wyr", e.wyrRounds}};
  if (e.attRounds > 0) {  // Around the Table is on
    games.push_back("att");
    rounds["att"] = e.attRounds;
  }
  return message("MATCH_STARTED", {{"games", games}, {"rounds", rounds}});
}

json toJson(const PhaseEvent& e) {
  json game = e.game == Game::None ? json(nullptr) : json(gameName(e.game));
  return message("PHASE", {{"game", game},
                           {"phase", phaseName(e.phase)},
                           {"round", e.round},
                           {"deadline", orNull(e.deadlineMs)}});
}

json toJson(const MatchOverEvent& e) {
  json board = json::array();
  for (const LeaderboardEntry& l : e.leaderboard) {
    board.push_back({{"playerId", l.playerId}, {"name", l.name}, {"score", l.score}});
  }
  json awards = json::array();
  for (const AwardSummary& a : e.awards) {
    awards.push_back({{"id", a.id}, {"title", a.title}, {"playerIds", a.playerIds}});
  }
  return message("MATCH_OVER",
                 {{"winnerIds", e.winnerIds}, {"leaderboard", board}, {"awards", awards}});
}

json toJson(const YourWordEvent& e) {
  return message("YOUR_WORD", {{"word", e.word}, {"isImpostor", e.isImpostor}});
}

json toJson(const SubmittedEvent& e) {
  return message("SUBMITTED", {{"playerIds", e.playerIds}});
}

json toJson(const VoteResultEvent& e) {
  json votes = json::array();
  for (const Vote& v : e.votes) {
    votes.push_back({{"voterId", v.voterId}, {"targetId", orNull(v.targetId)}});
  }
  return message("VOTE_RESULT",
                 {{"votes", votes}, {"accusedId", orNull(e.accusedId)}, {"caught", e.caught}});
}

json toJson(const RoundResultEvent& e) {
  return message("ROUND_RESULT", {{"impostorId", e.impostorId},
                                  {"mainWord", e.mainWord},
                                  {"impostorWord", e.impostorWord},
                                  {"guess", orNull(e.guess)},
                                  {"guessCorrect", orNull(e.guessCorrect)},
                                  {"void", e.voided},
                                  {"earlyGuess", e.earlyGuess},
                                  {"points", pointsToJson(e.points)}});
}

json toJson(const WyrPromptEvent& e) {
  return message("WYR_PROMPT", {{"optionA", e.optionA}, {"optionB", e.optionB}});
}

json toJson(const WyrResultEvent& e) {
  json choices = json::array();
  for (const WyrChoice& c : e.choices) {
    choices.push_back({{"playerId", c.playerId}, {"choice", std::string(1, c.choice)}});
  }
  return message("WYR_RESULT", {{"choices", choices},
                                {"countA", e.countA},
                                {"countB", e.countB},
                                {"majority", optionToJson(e.majority)},
                                {"points", pointsToJson(e.points)}});
}

json toJson(const AttSubjectEvent& e) {
  return message("ATT_SUBJECT", {{"subjectId", e.subjectId}, {"round", e.round}, {"total", e.total}});
}

json toJson(const AttPreviewEvent& e) {
  return message("ATT_PREVIEW",
                 {{"question", e.question}, {"options", e.options}, {"skipsLeft", e.skipsLeft}});
}

json toJson(const AttQuestionEvent& e) {
  return message("ATT_QUESTION",
                 {{"subjectId", e.subjectId}, {"question", e.question}, {"options", e.options}});
}

json toJson(const AttResultEvent& e) {
  json predictions = json::array();
  for (const AttPrediction& p : e.predictions) {
    predictions.push_back({{"playerId", p.playerId}, {"option", optionToJson(p.option)}});
  }
  return message("ATT_RESULT", {{"subjectId", e.subjectId},
                                {"answer", optionToJson(e.answer)},
                                {"predictions", predictions},
                                {"perfect", e.perfect},
                                {"points", pointsToJson(e.points)}});
}

json toJson(const AwardEvent& e) {
  return message("AWARD", {{"index", e.index},
                           {"total", e.total},
                           {"id", e.id},
                           {"title", e.title},
                           {"description", e.description},
                           {"playerIds", e.playerIds},
                           {"points", e.points}});
}

}  // namespace

json eventToJson(const Event& event) {
  return std::visit([](const auto& e) { return toJson(e); }, event);
}

json joinedMessage(const std::string& playerId, const std::string& token, const std::string& code) {
  return message("JOINED", {{"playerId", playerId}, {"token", token}, {"code", code}});
}

json errorMessage(const std::string& reason) {
  return message("ERROR", {{"reason", reason}});
}

}  // namespace feast
