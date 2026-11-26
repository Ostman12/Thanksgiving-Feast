#include "game/content.h"

namespace feast {

// Each pair is close enough that the impostor can bluff, different enough that
// a careful clue gives them away.
const std::vector<WordPair>& wordPairs() {
  static const std::vector<WordPair> pairs = {
      {"Turkey", "Chicken"},
      {"Pumpkin Pie", "Apple Pie"},
      {"Cranberry Sauce", "Gravy"},
      {"Stuffing", "Mashed Potatoes"},
      {"Pilgrim", "Cowboy"},
      {"Football", "Basketball"},
      {"Parade", "Concert"},
      {"Cornucopia", "Fruit Basket"},
      {"Sweet Potato", "Carrot"},
      {"Leftovers", "Takeout"},
      {"Thanksgiving", "Christmas"},
      {"Wishbone", "Drumstick"},
      {"Nap", "Bedtime"},
      {"Oven", "Microwave"},
      {"Corn", "Peas"},
      {"Grandma", "Aunt"},
      {"Black Friday", "Cyber Monday"},
      {"Autumn Leaves", "Snowflakes"},
      {"Scarecrow", "Snowman"},
      {"Apple Cider", "Hot Chocolate"},
      {"Dinner Rolls", "Cornbread"},
      {"Casserole", "Lasagna"},
      {"Tablecloth", "Napkin"},
      {"Fireplace", "Candle"},
      {"Road Trip", "Flight"},
      {"Pecan Pie", "Cheesecake"},
      {"Whipped Cream", "Ice Cream"},
      {"Family Photo", "Selfie"},
      {"Harvest", "Picnic"},
      {"Kids Table", "High Chair"},
  };
  return pairs;
}

// Both options should hurt a little, so the room actually splits.
const std::vector<WyrPrompt>& wyrPrompts() {
  static const std::vector<WyrPrompt> prompts = {
      {"Eat only cranberry sauce for a year", "Never eat pie again"},
      {"Host Thanksgiving for 30 relatives", "Spend Thanksgiving alone at an airport"},
      {"Carve the turkey on live TV", "Do the dishes for the whole street"},
      {"Have gravy instead of water forever", "Have no gravy ever again"},
      {"Sit at the kids table until you're 40", "Give a speech before every meal"},
      {"Eat a whole raw onion", "Drink a cup of turkey grease"},
      {"Lose the remote during the big game", "Have the oven die mid-turkey"},
      {"Only eat leftovers for a month", "Never have leftovers again"},
      {"Tell everyone your salary", "Let Grandma read your texts"},
      {"Wear a turkey costume to work for a week", "Wear a pilgrim hat every day for a year"},
      {"Black Friday shopping at 3 a.m.", "Wrap 100 presents with no tape"},
      {"Eat stuffing out of the bird", "Eat sweet potatoes with marshmallows for every meal"},
      {"Have every dinner end in a political debate", "Have every dinner in total silence"},
      {"Cook the whole feast alone", "Clean up the whole feast alone"},
      {"Fall asleep at the table", "Spill gravy on the host"},
      {"Give up mashed potatoes forever", "Give up dinner rolls forever"},
      {"Let your phone post your thoughts all dinner", "Let your family pick your outfit"},
      {"Eat a turkey drumstick in one bite", "Eat a whole pie with no hands"},
      {"Drive 12 hours to dinner", "Fly with three connecting flights"},
      {"Have a dog eat the turkey", "Have the turkey still frozen at noon"},
  };
  return prompts;
}

}  // namespace feast
