#include "../../libs/lcd.h"

const RunLengthPicture test_image = {
  16, 16,
  {
    {15, 1, "\377\377"},
    {14, 2, "\000\000\000\000"},
    {13, 2, "\000\377\000\000"},
    {12, 1, "\000\000"},
    {11, 1, "\000\000"},
    {10, 1, "\000\000"},
    { 9, 1, "\000\000"},
    { 8, 1, "\000\000"},
    { 7, 1, "\000\000"},
    { 6, 1, "\000\000"},
    { 5, 1, "\000\000"},
    { 4, 1, "\000\000"},
    { 3, 1, "\000\000"},
    { 2, 1, "\000\000"},
    { 1, 5, "\377\377\377\377\377\377\377\377\377\377"},
    { 0, 5, "\000\000\000\000\000\000\000\000\000\000"},
  }
};
