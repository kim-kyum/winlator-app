package com.winlator.s11;

import java.time.DayOfWeek;
import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.time.zone.ZoneOffsetTransitionRule;
import java.time.zone.ZoneRules;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * The device's time zone for Wine, as a POSIX TZ string ("KST-9",
 * "CET-1CEST,M3.5.0,M10.5.0/3").
 *
 * The rootfs has no time zone database, so glibc and with it Wine ran on UTC: clocks were
 * 9 hours behind in Korea. A POSIX rule string needs no database; Wine works out the
 * Windows time zone from the offsets, the daylight saving dates and the abbreviation
 * ("KST" picks Korea Standard Time over Tokyo).
 */
public final class TimeZones {
    private TimeZones() {}

    public static String posixTz() {
        return posixTz(ZoneId.systemDefault(), Instant.now());
    }

    public static String posixTz(ZoneId zone, Instant now) {
        ZoneRules rules = zone.getRules();
        List<ZoneOffsetTransitionRule> transitions = rules.getTransitionRules();
        ZoneOffset standard = rules.getStandardOffset(now);

        if (transitions.size() == 2) {
            ZoneOffsetTransitionRule a = transitions.get(0), b = transitions.get(1);
            // daylight saving time is the period whose offset differs from the standard one
            ZoneOffsetTransitionRule start = !a.getOffsetAfter().equals(a.getStandardOffset()) ? a : b;
            ZoneOffsetTransitionRule end = start == a ? b : a;
            ZoneOffset std = start.getStandardOffset();
            ZoneOffset dst = start.getOffsetAfter();
            if (!dst.equals(std)) {
                StringBuilder sb = new StringBuilder();
                sb.append(name(zone, false, std)).append(offset(std));
                sb.append(name(zone, true, dst));
                if (dst.getTotalSeconds() - std.getTotalSeconds() != 3600) sb.append(offset(dst));
                sb.append(',').append(rule(start)).append(',').append(rule(end));
                return sb.toString();
            }
        }

        // no daylight saving time (or rules POSIX can't express): the offset in force now
        ZoneOffset current = rules.getOffset(now);
        boolean daylight = !current.equals(standard);
        return name(zone, daylight, current) + offset(current);
    }

    /** The abbreviation when it is a plain one ("KST"), else the offset in angle brackets ("<+0530>"). */
    private static String name(ZoneId zone, boolean daylight, ZoneOffset offset) {
        String name = TimeZone.getTimeZone(zone).getDisplayName(daylight, TimeZone.SHORT, Locale.ROOT);
        if (name != null && name.matches("[A-Za-z]{3,6}")) return name;
        int seconds = offset.getTotalSeconds();
        int minutes = Math.abs(seconds) / 60;
        String sign = seconds < 0 ? "-" : "+";
        return minutes % 60 == 0 ? String.format(Locale.ROOT, "<%s%02d>", sign, minutes / 60)
                                 : String.format(Locale.ROOT, "<%s%02d%02d>", sign, minutes / 60, minutes % 60);
    }

    /** POSIX offsets count west of Greenwich: UTC+9 is "-9". */
    private static String offset(ZoneOffset offset) {
        return clock(-offset.getTotalSeconds());
    }

    private static String clock(int seconds) {
        String sign = seconds < 0 ? "-" : "";
        seconds = Math.abs(seconds);
        int h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60;
        if (s != 0) return String.format(Locale.ROOT, "%s%d:%02d:%02d", sign, h, m, s);
        if (m != 0) return String.format(Locale.ROOT, "%s%d:%02d", sign, h, m);
        return sign + h;
    }

    /** "Mm.w.d/time": the w-th (5 = last) weekday d of month m, at local time before the change. */
    private static String rule(ZoneOffsetTransitionRule rule) {
        int month = rule.getMonth().getValue();
        int dom = rule.getDayOfMonthIndicator();
        DayOfWeek dow = rule.getDayOfWeek();
        String date;
        if (dow == null) {
            // a fixed date: "Jn", day of a year without 29 February
            date = "J" + LocalDate.of(2001, month, Math.max(1, Math.min(dom, rule.getMonth().minLength()))).getDayOfYear();
        }
        else {
            int week = dom < 0 || dom + 6 >= rule.getMonth().minLength() ? 5 : (dom - 1) / 7 + 1;
            date = "M" + month + "." + week + "." + (dow.getValue() % 7);
        }

        // the time of the change on the clock before it
        int seconds = rule.getLocalTime().toSecondOfDay() + (rule.isMidnightEndOfDay() ? 86400 : 0);
        int before = rule.getOffsetBefore().getTotalSeconds();
        switch (rule.getTimeDefinition()) {
            case UTC: seconds += before; break;
            case STANDARD: seconds += before - rule.getStandardOffset().getTotalSeconds(); break;
            default: break;
        }
        return seconds == 7200 ? date : date + "/" + clock(seconds);
    }
}
