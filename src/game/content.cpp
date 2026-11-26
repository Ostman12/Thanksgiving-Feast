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

}  // namespace feast
