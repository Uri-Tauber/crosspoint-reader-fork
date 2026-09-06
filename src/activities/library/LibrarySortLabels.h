#pragma once

#include <I18n.h>
#include <LibrarySort.h>

inline const char* librarySortLabel(library::SortKind kind, bool compact = false) {
  if (compact && kind == library::SortKind::Date) return tr(STR_LIBRARY_TAB_DATE);
  if (compact && kind == library::SortKind::Subject) return tr(STR_LIBRARY_TAB_SUBJECT);
  if (!compact && kind == library::SortKind::Added) return tr(STR_LIBRARY_SORT_ADDED);
  static constexpr StrId LABELS[] = {
      StrId::STR_LIBRARY_TAB_TIME,    StrId::STR_LIBRARY_TAB_TITLE,      StrId::STR_LIBRARY_TAB_AUTHOR,
      StrId::STR_LIBRARY_SORT_DATE,   StrId::STR_LIBRARY_SORT_PUBLISHER, StrId::STR_LIBRARY_SORT_LANGUAGE,
      StrId::STR_LIBRARY_SORT_SERIES, StrId::STR_LIBRARY_SORT_SUBJECT,
  };
  static_assert(sizeof(LABELS) / sizeof(LABELS[0]) == library::SORT_COUNT);
  return I18N.get(LABELS[static_cast<unsigned>(kind)]);
}
