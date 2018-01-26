/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2018 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 */

#include "time.hpp"
#include "time-custom-clock.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>

namespace ndn {
namespace time {

static shared_ptr<CustomSystemClock> g_systemClock;
static shared_ptr<CustomSteadyClock> g_steadyClock;

// this function is declared in time-custom-clock.hpp
void
setCustomClocks(shared_ptr<CustomSteadyClock> steadyClock,
                shared_ptr<CustomSystemClock> systemClock)
{
  g_systemClock = std::move(systemClock);
  g_steadyClock = std::move(steadyClock);
}

/////////////////////////////////////////////////////////////////////////////////////////////

system_clock::time_point
system_clock::now() noexcept
{
  if (g_systemClock == nullptr) {
    // optimized default version
    return time_point(boost::chrono::system_clock::now().time_since_epoch());
  }
  else {
    return g_systemClock->getNow();
  }
}

std::time_t
system_clock::to_time_t(const system_clock::time_point& t) noexcept
{
  return duration_cast<seconds>(t.time_since_epoch()).count();
}

system_clock::time_point
system_clock::from_time_t(std::time_t t) noexcept
{
  return time_point(seconds(t));
}

/////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __APPLE__
  // Note that on OS X platform boost::steady_clock is not truly monotonic, so we use
  // system_clock instead.  Refer to https://svn.boost.org/trac/boost/ticket/7719)
  typedef boost::chrono::system_clock base_steady_clock;
#else
  typedef boost::chrono::steady_clock base_steady_clock;
#endif

steady_clock::time_point
steady_clock::now() noexcept
{
  if (g_steadyClock == nullptr) {
    // optimized default version
    return time_point(base_steady_clock::now().time_since_epoch());
  }
  else {
    return g_steadyClock->getNow();
  }
}

steady_clock::duration
steady_clock::to_wait_duration(steady_clock::duration d)
{
  if (g_steadyClock == nullptr) {
    // optimized default version
    return d;
  }
  else {
    return g_steadyClock->toWaitDuration(d);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////

const system_clock::TimePoint&
getUnixEpoch()
{
  static auto epoch = system_clock::from_time_t(0);
  return epoch;
}

milliseconds
toUnixTimestamp(const system_clock::TimePoint& point)
{
  return duration_cast<milliseconds>(point - getUnixEpoch());
}

system_clock::TimePoint
fromUnixTimestamp(milliseconds duration)
{
  return getUnixEpoch() + duration;
}

std::string
toIsoString(const system_clock::TimePoint& timePoint)
{
  namespace bpt = boost::posix_time;
  static bpt::ptime epoch(boost::gregorian::date(1970, 1, 1));

#ifdef BOOST_DATE_TIME_POSIX_TIME_STD_CONFIG
  using BptResolutionUnit = nanoseconds;
#else
  using BptResolutionUnit = microseconds;
#endif
  constexpr auto unitsPerHour = duration_cast<BptResolutionUnit>(hours(1)).count();

  auto sinceEpoch = duration_cast<BptResolutionUnit>(timePoint - getUnixEpoch()).count();
  bpt::ptime ptime = epoch + bpt::time_duration(sinceEpoch / unitsPerHour, 0, 0, sinceEpoch % unitsPerHour);

  return bpt::to_iso_string(ptime);
}

static system_clock::TimePoint
convertToTimePoint(const boost::posix_time::ptime& ptime)
{
  namespace bpt = boost::posix_time;
  static bpt::ptime epoch(boost::gregorian::date(1970, 1, 1));

  // .total_seconds() has issue with large dates until Boost 1.66. See Issue #4478
  // from_time_t has issues with large dates on 32-bit platforms
  auto point = system_clock::time_point(seconds((ptime - epoch).ticks() / bpt::time_duration::ticks_per_second()));
  point += microseconds((ptime - epoch).total_microseconds() % 1000000);
  return point;
}

system_clock::TimePoint
fromIsoString(const std::string& isoString)
{
  return convertToTimePoint(boost::posix_time::from_iso_string(isoString));
}

std::string
toString(const system_clock::TimePoint& timePoint,
         const std::string& format/* = "%Y-%m-%d %H:%M:%S"*/,
         const std::locale& locale/* = std::locale("C")*/)
{
  namespace bpt = boost::posix_time;
  bpt::ptime ptime = bpt::from_time_t(system_clock::to_time_t(timePoint));

  uint64_t micro = duration_cast<microseconds>(timePoint - getUnixEpoch()).count() % 1000000;
  ptime += bpt::microseconds(micro);

  bpt::time_facet* facet = new bpt::time_facet(format.c_str());
  std::ostringstream formattedTimePoint;
  formattedTimePoint.imbue(std::locale(locale, facet));
  formattedTimePoint << ptime;

  return formattedTimePoint.str();
}

system_clock::TimePoint
fromString(const std::string& timePointStr,
           const std::string& format/* = "%Y-%m-%d %H:%M:%S"*/,
           const std::locale& locale/* = std::locale("C")*/)
{
  namespace bpt = boost::posix_time;

  bpt::time_input_facet* facet = new bpt::time_input_facet(format);
  std::istringstream is(timePointStr);
  is.imbue(std::locale(locale, facet));
  bpt::ptime ptime;
  is >> ptime;

  return convertToTimePoint(ptime);
}

} // namespace time
} // namespace ndn

namespace boost {
namespace chrono {

template<class CharT>
std::basic_string<CharT>
clock_string<ndn::time::system_clock, CharT>::since()
{
  if (ndn::time::g_systemClock == nullptr) {
    // optimized default version
    return clock_string<system_clock, CharT>::since();
  }
  else {
    return ndn::time::g_systemClock->getSince();
  }
}

template<class CharT>
std::basic_string<CharT>
clock_string<ndn::time::steady_clock, CharT>::since()
{
  if (ndn::time::g_steadyClock == nullptr) {
    // optimized default version
    return clock_string<ndn::time::base_steady_clock, CharT>::since();
  }
  else {
    return ndn::time::g_steadyClock->getSince();
  }
}

template struct clock_string<ndn::time::system_clock, char>;
template struct clock_string<ndn::time::steady_clock, char>;

} // namespace chrono
} // namespace boost
