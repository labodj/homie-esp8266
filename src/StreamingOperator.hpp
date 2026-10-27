#pragma once

template<class T>
inline Print &operator <<(Print &stream, T arg)
{ stream.print(arg); return stream; }

inline Print &operator <<(Print &stream, const String& arg)
{ stream.print(arg); return stream; }

enum _EndLineCode { endl };

inline Print &operator <<(Print &stream, _EndLineCode arg)
{ stream.println(); return stream; }
