#ifndef _GIME_H_
#define _GIME_H_


template <typename T>
struct DontGime {
  constexpr static bool DoesGime() { return false; }
};
template <typename T>
struct DoGime {
  constexpr static bool DoesGime() { return true; }
};


#endif // _GIME_H_
