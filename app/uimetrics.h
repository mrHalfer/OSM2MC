#pragma once
// Common vertical metrics of the two window columns, so that the controls on the left and on the right stand on the same levels.

constexpr int kControlH = 34;       //!< the height of a button / input / combo box (20 px content + padding and border from tmc.qss)
constexpr int kHeaderH = 44;        //!< the top band: the title and language on the left, the search row / "Load the map" row on the right
constexpr int kCardTitleH = 12;     //!< the part of a group box above its frame (the title area), the map on the right is shifted by it
constexpr int kRowGap = 12;         //!< the vertical gap between rows in both columns
