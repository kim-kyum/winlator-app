package com.winlator.s11;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.text.format.DateUtils;

import androidx.core.content.FileProvider;

import com.winlator.R;
import com.winlator.core.FileUtils;
import com.winlator.xenvironment.RootFS;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Why programs did not start or quit, for the Troubleshooting page of the S11 Settings app
 * (s11settings.c) and the taskbar's notices (s11shell.c). A program that cannot start shows
 * nothing at all in Wine, so without this the user only sees that nothing happens.
 *
 * Two sources:
 *  - Wine's output. A missing DLL stops a program before it shows anything, and Wine says so
 *    only on the module channel's errors, which WINEDEBUG keeps; unhandled exceptions and
 *    winedbg's report come whatever WINEDEBUG says.
 *  - Unity's Player.log of each game (AppData\LocalLow\company\product): Unity catches its own
 *    crashes, so Wine never hears of them.
 *
 * Files (Wine paths):
 *  - ~/.config/s11-problems.txt: "S11PROBLEMS 1 revision", then the page's rows as in
 *    s11-container.txt (page "problems"): section; log (title, detail, value = the Windows
 *    path of a text file the page opens or shares); info; action.
 *  - ~/.config/s11-notice.txt: "S11NOTICE 1 serial", a title line and a text line. The taskbar
 *    shows each new serial once.
 *  - C:\S11\Logs: a report for each problem, and wine-errors.txt (Wine's last error lines).
 *
 * Commands come over winhandler code 109: problems.refresh, problems.clear and
 * problems.share (value: one of the page's Windows paths), which hands the file to Android's
 * share sheet.
 */
public final class S11Problems {
    public static final String FILE_NAME = "s11-problems.txt";
    public static final String NOTICE_FILE_NAME = "s11-notice.txt";
    /** WINEDEBUG unless the user asked for a debug log: the module channel's errors name a missing DLL */
    public static final String WINEDEBUG = "-all,err+module";

    private static final int MAX_PROBLEMS = 12;
    private static final int MAX_LINES = 300;
    private static final int MAX_REPORT_LINES = 400;
    private static final int MAX_UNITY_LOGS = 8;
    private static final int MAX_LOG_FILES = 30;
    private static final long SCAN_INTERVAL_MS = 4000;
    private static final long IMPORT_WAIT_MS = 3000;
    private static final long SAME_PROBLEM_MS = 60000;
    private static final long CRASH_NOTICE_WAIT_MS = 4000;
    private static final long CRASH_REPORT_IDLE_MS = 2000;
    private static final String LOGS_DIR = "drive_c/S11/Logs";   // in the prefix
    private static final String LOGS_WINDOWS = "C:\\S11\\Logs\\";
    private static final String WINE_LOG_NAME = "wine-errors.txt";

    // Wine's debug lines start with the thread (and maybe a time stamp): "0124:err:module:import_dll ..."
    private static final Pattern ERR_LINE = Pattern.compile("^(?:[0-9. ]+:)?(?:[0-9a-f]{4,8}:)*err:(\\w+):(\\w+) (.*)$");
    private static final Pattern IMPORT_NOT_FOUND = Pattern.compile("^Library (\\S+) \\(which is needed by (L\".*\")\\) not found");
    private static final Pattern IMPORT_FAILED = Pattern.compile("^Loading library (\\S+) \\(which is needed by (L\".*\")\\) failed \\(error ([0-9a-fA-F]+)\\)");
    private static final Pattern IMPORTS_FAILED = Pattern.compile("^(?:Importing|Initializing) dlls for (L\".*\") failed, status ([0-9a-fA-F]+)");
    private static final Pattern DLL_INIT_FAILED = Pattern.compile("^\"(.*)\" failed to initialize, aborting");
    private static final Pattern UNHANDLED = Pattern.compile("^wine: (.+?) at address (\\S+) \\(thread ([0-9a-fA-F]+)\\), starting debugger");
    private static final Pattern START_FAILED = Pattern.compile("^wine: (?:failed to start|failed to open|could not load) (.*)$");
    private static final Pattern WINE_STRING = Pattern.compile("L\"(?:[^\"\\\\]|\\\\.)*\"");
    private static final Pattern DEBUGGEE =Pattern.compile("^[0-9a-fA-F]{8} \\(D\\) (.+)$");
    private static final Pattern FAULT_MODULE = Pattern.compile("^=>\\s*0 0x[0-9a-fA-F]+ in (\\S+)");
    // Unity's Player.log
    private static final Pattern UNITY_CAUSED = Pattern.compile("caused an? (.+?) \\((0x[0-9a-fA-F]{8})\\)");
    private static final Pattern UNITY_MODULE = Pattern.compile("in module (\\S+)");
    private static final Pattern UNITY_DLL = Pattern.compile("DllNotFoundException: (?:Unable to load DLL ')?([^'\\s:]+)");
    private static final Pattern UNITY_GRAPHICS = Pattern.compile("(?i)(failed to (create|initiali[sz]e)[^\\n]*(d3d|direct3d|device|graphics|vulkan|swap ?chain))");
    private static final Pattern UNITY_EXCEPTION = Pattern.compile("^(\\w+(?:\\.\\w+)*Exception): ?(.*)$", Pattern.MULTILINE);

    private static final class Problem {
        long time;
        String program = "";    // the file name; empty until winedbg names it
        String title = "", detail = "";
        String reason = "";     // a crash's cause, before winedbg names the module
        String head = "";       // Wine's line about it
        File report;            // C:\S11\Logs\...
        boolean crash, named, moduleKnown, noticed;
    }

    private static final class UnityLog {
        File file;
        String product = "";
        long modified = -1, length = -1, checkedModified = -1, notifiedModified = -1;
        long changedAt;
        String summary = "";
        int severity;           // 2: a crash or a Steam restart (the taskbar tells), 1: worth a look
    }

    private final Activity activity;
    private final File rootDir, prefixDir, configFile, noticeFile, logsDir, shareDir;
    private final HandlerThread thread = new HandlerThread("S11Problems");
    private final Handler handler;
    private final long sessionStart = System.currentTimeMillis();
    private final AtomicInteger forwardLines = new AtomicInteger();

    // all of these on the handler thread
    private final ArrayDeque<String> lines = new ArrayDeque<>();
    private final ArrayList<Problem> problems = new ArrayList<>();
    private final HashMap<String, UnityLog> unityLogs = new HashMap<>();
    private final ArrayList<String[]> pendingImports = new ArrayList<>();   // {dll, needer, status}
    private int revision = (int)(System.currentTimeMillis() / 1000 % 1000000);
    private int noticeSerial = revision;
    private String written = "";
    private boolean started, wineLogDirty;
    private Problem crash;                  // the crash winedbg is reporting
    private ArrayList<String> crashReport;  // winedbg's lines
    private int crashTail = -1;             // lines left after "System information:"

    private final Runnable scanner = new Runnable() {
        @Override
        public void run() {
            scanUnityLogs();
            write();
            handler.postDelayed(this, SCAN_INTERVAL_MS);
        }
    };
    private final Runnable importsTimeout = this::flushImports;
    private final Runnable crashNotice = this::noticeCrash;
    private final Runnable crashFinish = this::finishCrashReport;
    private final Runnable wineLogWriter = this::writeWineLog;

    public S11Problems(Activity activity) {
        this.activity = activity;
        rootDir = RootFS.find(activity).getRootDir();
        prefixDir = new File(rootDir, RootFS.WINEPREFIX);
        configFile = new File(rootDir, RootFS.USER_CONFIG_PATH + "/" + FILE_NAME);
        noticeFile = new File(rootDir, RootFS.USER_CONFIG_PATH + "/" + NOTICE_FILE_NAME);
        logsDir = new File(prefixDir, LOGS_DIR);
        shareDir = new File(activity.getFilesDir(), "s11-share");
        thread.start();
        handler = new Handler(thread.getLooper());
    }

    public void start() {
        handler.post(() -> {
            if (started) return;
            started = true;
            scanner.run();
        });
    }

    public void stop() {
        handler.post(() -> {
            started = false;
            handler.removeCallbacksAndMessages(null);
            if (wineLogDirty) writeWineLog();
            thread.quitSafely();
        });
    }

    // ------------------------------------------------------------------------------------
    // Wine's output
    // ------------------------------------------------------------------------------------

    /** Every line the Wine session prints (ProcessHelper's reader thread): keep this cheap. */
    public void onWineOutput(String line) {
        if (line == null || line.isEmpty()) return;
        boolean wanted;
        if (line.startsWith("Unhandled exception: ") || (line.startsWith("wine: ") && line.contains("starting debugger"))) {
            // winedbg's report follows (the lines that come next are not marked)
            forwardLines.set(MAX_REPORT_LINES);
            wanted = true;
        }
        else if (line.startsWith("wine: ")) wanted = !line.contains("pre exec");   // Winlator's hook, every program
        else wanted = line.contains("err:") || forwardLines.getAndUpdate(n -> Math.max(0, n - 1)) > 0;
        if (wanted) handler.post(() -> onLine(line));
    }

    private void onLine(String line) {
        lines.addLast(line);
        while (lines.size() > MAX_LINES) lines.removeFirst();
        wineLogDirty = true;
        handler.removeCallbacks(wineLogWriter);
        handler.postDelayed(wineLogWriter, 2000);

        if (line.startsWith("Unhandled exception: ")) {
            // winedbg starts its report (after the user closed Wine's crash dialog, if it showed)
            crashReport = new ArrayList<>();
            crashTail = -1;
            collectCrashReport(line);
            return;
        }
        if (crashReport != null) collectCrashReport(line);

        Matcher m = ERR_LINE.matcher(line);
        if (m.matches()) {
            if (m.group(1).equals("module")) onModuleError(m.group(3));
            return;
        }
        if ((m = UNHANDLED.matcher(line)).find()) {
            onUnhandled(m.group(1), line);
            return;
        }
        if ((m = START_FAILED.matcher(line)).find()) {
            Matcher name = WINE_STRING.matcher(m.group(1));
            Problem problem = newProblem(name.find() ? fileName(unescape(name.group())) : "",
                                         activity.getString(R.string.s11_problem_start_failed), line + "\n" + recentLines(20));
            notice(problem);
            write();
        }
    }

    private void onModuleError(String message) {
        Matcher m;
        if ((m = IMPORT_NOT_FOUND.matcher(message)).find()) {
            addPendingImport(m.group(1), unescape(m.group(2)), null);
        }
        else if ((m = IMPORT_FAILED.matcher(message)).find()) {
            addPendingImport(m.group(1), unescape(m.group(2)), m.group(3));
        }
        else if ((m = DLL_INIT_FAILED.matcher(message)).find()) {
            addPendingImport(m.group(1), "", "init");
        }
        else if ((m = IMPORTS_FAILED.matcher(message)).find()) {
            // the program itself could not start: what it was missing came just before
            String program = unescape(m.group(1));
            String status = m.group(2).toLowerCase(Locale.ENGLISH);
            handler.removeCallbacks(importsTimeout);
            StringBuilder report = new StringBuilder();
            String[] root = null;
            for (String[] item : pendingImports) {
                report.append(item[1].isEmpty() ? item[0] : item[0] + "  <-  " + item[1]).append('\n');
                // the first DLL not found is the cause; the DLLs that needed it failed after it
                if (root == null && item[2] == null) root = item;
            }
            if (root == null && !pendingImports.isEmpty()) root = pendingImports.get(0);
            String reason = root != null ? describeImport(root) : startReason(status);
            pendingImports.clear();
            Problem problem = newProblem(fileName(program), activity.getString(R.string.s11_problem_cannot_start, reason),
                                         program + "\n" + report + recentLines(40));
            notice(problem);
            write();
        }
    }

    private void addPendingImport(String dll, String needer, String status) {
        pendingImports.add(new String[]{dll, needer, status});
        if (pendingImports.size() == 1) handler.postDelayed(importsTimeout, IMPORT_WAIT_MS);
    }

    /** DLLs a running program could not load (a plugin, an optional part): noted, not announced */
    private void flushImports() {
        for (String[] item : pendingImports) {
            String needer = item[1];
            if (needer.isEmpty() || needer.toLowerCase(Locale.ENGLISH).startsWith("c:\\windows\\")) continue;
            newProblem(fileName(needer), activity.getString(R.string.s11_problem_part_failed, describeImport(item)),
                       needer + "\n" + recentLines(20));
        }
        pendingImports.clear();
        write();
    }

    /** {dll, needer, status}: status null = not found, "init" = its DllMain failed */
    private String describeImport(String[] item) {
        if (item[2] == null) return activity.getString(R.string.s11_problem_missing_dll, item[0]);
        if (item[2].equals("init")) return activity.getString(R.string.s11_problem_dll_init, item[0]);
        return activity.getString(R.string.s11_problem_dll_failed, item[0], statusText(item[2]));
    }

    private void onUnhandled(String what, String line) {
        String reason;
        String lower = what.toLowerCase(Locale.ENGLISH);
        if (lower.startsWith("unimplemented function ")) {
            reason = activity.getString(R.string.s11_problem_unimplemented, what.substring("Unimplemented function ".length()).replace(" called", ""));
        }
        else if (lower.contains("page fault")) reason = activity.getString(R.string.s11_problem_page_fault);
        else if (lower.contains("illegal instruction") || lower.contains("privileged instruction")) reason = activity.getString(R.string.s11_problem_illegal);
        else if (lower.contains("stack overflow")) reason = activity.getString(R.string.s11_problem_stack);
        else {
            Matcher code = Pattern.compile("0x[0-9a-fA-F]{8}").matcher(what);
            String number = code.find() ? code.group().toLowerCase(Locale.ENGLISH) : "";
            if (number.equals("0xc0000409")) reason = activity.getString(R.string.s11_problem_fail_fast);
            else if (number.equals("0xe06d7363")) reason = activity.getString(R.string.s11_problem_cpp_exception);
            else reason = activity.getString(R.string.s11_problem_exception, number.isEmpty() ? what : number);
        }
        Problem problem = newProblem("", activity.getString(R.string.s11_problem_crashed, reason), line + "\n");
        problem.crash = true;
        problem.reason = reason;
        problem.head = line;
        crash = problem;
        // winedbg names the program in a moment (unless Wine's crash dialog waits for the user)
        handler.removeCallbacks(crashNotice);
        handler.postDelayed(crashNotice, CRASH_NOTICE_WAIT_MS);
        write();
    }

    /** The crash's notice, with or without the program's name */
    private void noticeCrash() {
        handler.removeCallbacks(crashNotice);
        if (crash != null && !crash.noticed) {
            crash.noticed = true;
            notice(crash);
        }
    }

    /** winedbg's report ended (it prints nothing more after its system information) */
    private void finishCrashReport() {
        handler.removeCallbacks(crashFinish);
        if (crashReport == null) return;
        if (crash != null) {
            StringBuilder text = new StringBuilder();
            for (String l : crashReport) text.append(l).append('\n');
            saveReport(crash, crash.detail + "\n\n" + crash.head + "\n\n" + text);
            if (!crash.noticed) noticeCrash();
            else if (crash.named) notice(crash);   // again, now with the program's name
        }
        crashReport = null;
        crash = null;
        crashTail = -1;
        forwardLines.set(0);
        write();
    }

    private void collectCrashReport(String line) {
        crashReport.add(line);
        handler.removeCallbacks(crashFinish);
        handler.postDelayed(crashFinish, CRASH_REPORT_IDLE_MS);
        Matcher m;
        if (crash != null && !crash.named && (m = DEBUGGEE.matcher(line)).find()) {
            crash.program = fileName(m.group(1).trim());
            crash.named = true;
            crash.title = crash.program;
        }
        if (crash != null && !crash.moduleKnown && (m = FAULT_MODULE.matcher(line.trim())).find()) {
            // winedbg's backtrace: "=>0 0x00007ffb12345678 in unityplayer (+0x123456) ..."
            crash.moduleKnown = true;
            crash.detail = activity.getString(R.string.s11_problem_crashed_in, crash.reason, m.group(1));
        }
        if (line.startsWith("System information:")) crashTail = 8;   // Wine build, platform, versions
        else if (crashTail > 0) crashTail--;
        if (crashTail == 0 || crashReport.size() >= MAX_REPORT_LINES) finishCrashReport();
    }

    private Problem newProblem(String program, String detail, String report) {
        long now = System.currentTimeMillis();
        // the same failure again (the user tried once more): one row, the latest time
        for (Problem p : problems) {
            if (now - p.time < SAME_PROBLEM_MS && p.program.equals(program) && p.detail.equals(detail)) {
                p.time = now;
                saveReport(p, detail + "\n\n" + report);
                return p;
            }
        }
        Problem problem = new Problem();
        problem.time = now;
        problem.program = program;
        problem.named = !program.isEmpty();
        problem.title = program.isEmpty() ? activity.getString(R.string.s11_problem_unknown_program) : program;
        problem.detail = detail;
        problems.add(0, problem);
        while (problems.size() > MAX_PROBLEMS) problems.remove(problems.size() - 1);
        saveReport(problem, detail + "\n\n" + report);
        return problem;
    }

    private String startReason(String status) {
        return activity.getString(R.string.s11_problem_status, statusText(status));
    }

    private String statusText(String status) {
        switch (status.toLowerCase(Locale.ENGLISH)) {
            case "c0000135": return activity.getString(R.string.s11_problem_status_dll_not_found);
            case "c0000139": return activity.getString(R.string.s11_problem_status_entry_point);
            case "c000007b": return activity.getString(R.string.s11_problem_status_image_format);
            case "c0000142": return activity.getString(R.string.s11_problem_status_init_failed);
            default: return "0x" + status;
        }
    }

    private String recentLines(int count) {
        StringBuilder text = new StringBuilder("\n");
        int skip = Math.max(0, lines.size() - count), i = 0;
        for (String line : lines) if (i++ >= skip) text.append(line).append('\n');
        return text.toString();
    }

    /** Wine's debugstr_w: L"C:\\Games\\a\\c548.exe" (\XXXX for other characters) */
    static String unescape(String text) {
        if (text.startsWith("L\"")) text = text.substring(2);
        if (text.endsWith("\"")) text = text.substring(0, text.length() - 1);
        StringBuilder out = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c != '\\' || i + 1 >= text.length()) {
                out.append(c);
                continue;
            }
            char next = text.charAt(i + 1);
            if (next == 'n') { out.append('\n'); i++; }
            else if (next == 'r') { out.append('\r'); i++; }
            else if (next == 't') { out.append('\t'); i++; }
            else if (next == '"' || next == '\\') { out.append(next); i++; }
            else if (i + 4 < text.length() && isHex(text, i + 1, 4)) {
                out.append((char)Integer.parseInt(text.substring(i + 1, i + 5), 16));
                i += 4;
            }
            else out.append(c);
        }
        return out.toString();
    }

    private static boolean isHex(String text, int start, int count) {
        for (int i = start; i < start + count; i++) if (Character.digit(text.charAt(i), 16) < 0) return false;
        return true;
    }

    static String fileName(String path) {
        path = path.trim();
        int slash = Math.max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
        return slash >= 0 ? path.substring(slash + 1) : path;
    }

    // ------------------------------------------------------------------------------------
    // Unity
    // ------------------------------------------------------------------------------------

    private void scanUnityLogs() {
        File localLow = new File(prefixDir, "drive_c/users/" + RootFS.USER + "/AppData/LocalLow");
        File[] companies = localLow.listFiles();
        if (companies == null) return;
        long now = System.currentTimeMillis();
        for (File company : companies) {
            File[] products = company.listFiles();
            if (products == null) continue;
            for (File product : products) {
                File file = new File(product, "Player.log");
                if (!file.isFile()) continue;
                String key = file.getPath();
                UnityLog log = unityLogs.get(key);
                if (log == null) {
                    log = new UnityLog();
                    log.file = file;
                    log.product = product.getName();
                    unityLogs.put(key, log);
                }
                long modified = file.lastModified(), length = file.length();
                if (modified != log.modified || length != log.length) {
                    log.modified = modified;
                    log.length = length;
                    log.changedAt = now;
                }
                // read it once it has settled (the game may still be writing)
                if (log.checkedModified != log.modified && (now - log.changedAt >= 2500 || log.checkedModified == -1)) {
                    log.checkedModified = log.modified;
                    summarize(log);
                    if (log.severity >= 2 && log.modified > sessionStart && log.notifiedModified != log.modified) {
                        log.notifiedModified = log.modified;
                        writeNotice(log.product, log.summary);
                    }
                }
            }
        }
    }

    private void summarize(UnityLog log) {
        String text = readEnds(log.file, 32 * 1024, 96 * 1024);
        Matcher m;
        log.severity = 0;
        if (text.contains("RestartAppIfNecessary returned true") ||
            (text.contains("RestartAppIfNecessary") && text.contains("Steam will restart"))) {
            // Steamworks.NET's SteamManager: the game quits so that Steam starts it again
            log.summary = activity.getString(R.string.s11_problem_unity_steam);
            log.severity = 2;
        }
        else if (text.contains("Crash!!!") || (m = UNITY_CAUSED.matcher(text)).find()) {
            m = UNITY_CAUSED.matcher(text);
            if (m.find()) {
                String what = m.group(1) + " " + m.group(2);
                Matcher module = UNITY_MODULE.matcher(text.substring(m.end(), Math.min(text.length(), m.end() + 200)));
                if (module.find()) what += ", " + module.group(1);
                log.summary = activity.getString(R.string.s11_problem_unity_crash, what);
            }
            else log.summary = activity.getString(R.string.s11_problem_unity_crash_plain);
            log.severity = 2;
        }
        else if ((m = UNITY_GRAPHICS.matcher(text)).find()) {
            // Unity says so in a message box too, and some games fall back to another API
            log.summary = activity.getString(R.string.s11_problem_unity_graphics, m.group(1));
            log.severity = 1;
        }
        else if ((m = UNITY_DLL.matcher(text)).find()) {
            log.summary = activity.getString(R.string.s11_problem_unity_dll, m.group(1));
            log.severity = 1;
        }
        else if (text.contains("SteamAPI_Init() failed") || text.contains("SteamAPI.Init() failed") || text.contains("SteamAPI_Init failed")) {
            log.summary = activity.getString(R.string.s11_problem_unity_steam_init);
            log.severity = 1;
        }
        else if ((m = UNITY_EXCEPTION.matcher(text)).find()) {
            log.summary = activity.getString(R.string.s11_problem_unity_exception, m.group(1));
            log.severity = 1;
        }
        else log.summary = activity.getString(R.string.s11_problem_unity_last, lastLine(text));
    }

    private static String lastLine(String text) {
        String[] all = text.split("\n");
        for (int i = all.length - 1; i >= 0; i--) {
            String line = all[i].trim();
            if (line.isEmpty() || line.startsWith("(Filename:")) continue;
            return line.length() > 100 ? line.substring(0, 100) + "…" : line;
        }
        return "";
    }

    /** The first head and the last tail bytes of a file (the middle of a long log rarely says why) */
    private static String readEnds(File file, int head, int tail) {
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long length = in.length();
            byte[] data;
            if (length <= head + tail) {
                data = new byte[(int)length];
                in.readFully(data);
                return new String(data, StandardCharsets.UTF_8);
            }
            byte[] first = new byte[head], last = new byte[tail];
            in.readFully(first);
            in.seek(length - tail);
            in.readFully(last);
            return new String(first, StandardCharsets.UTF_8) + "\n…\n" + new String(last, StandardCharsets.UTF_8);
        }
        catch (IOException e) {
            return "";
        }
    }

    // ------------------------------------------------------------------------------------
    // Commands from the Settings app
    // ------------------------------------------------------------------------------------

    public static boolean isCommand(String key) {
        return key.startsWith("problems.");
    }

    public void apply(String key, String value) {
        handler.post(() -> {
            switch (key) {
                case "problems.refresh":
                    for (UnityLog log : unityLogs.values()) log.checkedModified = -1;
                    scanUnityLogs();
                    if (wineLogDirty) writeWineLog();
                    break;
                case "problems.clear":
                    problems.clear();
                    break;
                case "problems.share":
                    share(value);
                    break;
            }
            write();
        });
    }

    /** A C: or Z: path of the page, as a file of the rootfs (nothing outside it) */
    private File fromWindowsPath(String path) {
        File file;
        if (path.length() > 3 && (path.startsWith("C:\\") || path.startsWith("c:\\")))
            file = new File(prefixDir, "drive_c/" + path.substring(3).replace('\\', '/'));
        else if (path.length() > 3 && (path.startsWith("Z:\\") || path.startsWith("z:\\")))
            file = new File(rootDir, path.substring(3).replace('\\', '/'));
        else return null;
        try {
            String root = rootDir.getCanonicalPath() + "/";
            if (!file.getCanonicalPath().startsWith(root)) return null;
        }
        catch (IOException e) {
            return null;
        }
        return file.isFile() ? file : null;
    }

    private void share(String path) {
        File source = fromWindowsPath(path);
        if (source == null) return;
        // a copy with a name that says what it is, as plain text
        String name = source.getName();
        if (name.equals("Player.log") && source.getParentFile() != null) name = source.getParentFile().getName() + " - Player.log";
        name = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        if (!name.toLowerCase(Locale.ENGLISH).endsWith(".txt")) name += ".txt";
        if (!shareDir.isDirectory()) shareDir.mkdirs();
        File[] old = shareDir.listFiles();
        if (old != null) for (File file : old) FileUtils.delete(file);
        File copy = new File(shareDir, name);
        if (!FileUtils.copy(source, copy)) return;

        activity.runOnUiThread(() -> {
            try {
                Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".FileProvider", copy);
                Intent send = new Intent(Intent.ACTION_SEND)
                    .setType("text/plain")
                    .putExtra(Intent.EXTRA_STREAM, uri)
                    .putExtra(Intent.EXTRA_SUBJECT, copy.getName())
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                Intent chooser = Intent.createChooser(send, activity.getString(R.string.s11_problem_share_title));
                chooser.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                activity.startActivity(chooser);
            }
            catch (Exception e) {
                e.printStackTrace();
            }
        });
    }

    // ------------------------------------------------------------------------------------
    // Files
    // ------------------------------------------------------------------------------------

    private void saveReport(Problem problem, String text) {
        if (!logsDir.isDirectory() && !logsDir.mkdirs()) return;
        if (problem.report == null) {
            String stamp = new SimpleDateFormat("yyyy-MM-dd HHmmss", Locale.ENGLISH).format(new Date(problem.time));
            String program = problem.program.isEmpty() ? "program" : problem.program.replaceAll("[\\\\/:*?\"<>|]", "_");
            problem.report = new File(logsDir, stamp + " " + program + ".txt");
        }
        else if (problem.named && !problem.report.getName().contains(problem.program)) {
            // the crash got its program's name: rename the report
            String stamp = new SimpleDateFormat("yyyy-MM-dd HHmmss", Locale.ENGLISH).format(new Date(problem.time));
            File renamed = new File(logsDir, stamp + " " + problem.program.replaceAll("[\\\\/:*?\"<>|]", "_") + ".txt");
            if (problem.report.renameTo(renamed)) problem.report = renamed;
        }
        String header = problem.title + "\r\n" + DateUtils.formatDateTime(activity, problem.time,
            DateUtils.FORMAT_SHOW_DATE | DateUtils.FORMAT_SHOW_TIME | DateUtils.FORMAT_SHOW_YEAR) + "\r\n\r\n";
        writeText(problem.report, header + text.replace("\r\n", "\n").replace("\n", "\r\n"));
        pruneLogs();
    }

    private void pruneLogs() {
        File[] files = logsDir.listFiles((dir, name) -> name.endsWith(".txt") && !name.equals(WINE_LOG_NAME));
        if (files == null || files.length <= MAX_LOG_FILES) return;
        Arrays.sort(files, (a, b) -> a.getName().compareTo(b.getName()));
        for (int i = 0; i < files.length - MAX_LOG_FILES; i++) FileUtils.delete(files[i]);
    }

    private void writeWineLog() {
        wineLogDirty = false;
        if (!logsDir.isDirectory() && !logsDir.mkdirs()) return;
        StringBuilder text = new StringBuilder();
        for (String line : lines) text.append(line).append("\r\n");
        writeText(new File(logsDir, WINE_LOG_NAME), text.toString());
        write();
    }

    /** UTF-8 with a byte order mark, so Notepad shows Hangul */
    private static void writeText(File file, String text) {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(new byte[]{(byte)0xef, (byte)0xbb, (byte)0xbf});
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
        catch (IOException e) {}
    }

    private void notice(Problem problem) {
        writeNotice(problem.title, problem.detail);
    }

    private void writeNotice(String title, String text) {
        writeAtomically(noticeFile, "S11NOTICE\t1\t" + (++noticeSerial) + "\n" + clean(title) + "\n" + clean(text) + "\n");
    }

    private static String clean(String text) {
        return text == null ? "" : text.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    private static void row(StringBuilder out, String type, String key, String title, String detail, String value) {
        out.append("ROW\tproblems\t").append(type).append('\t').append(clean(key)).append('\t').append(clean(title))
           .append('\t').append(clean(detail)).append('\t').append(clean(value)).append('\n');
    }

    private String when(long time) {
        boolean today = DateUtils.isToday(time);
        return DateUtils.formatDateTime(activity, time, DateUtils.FORMAT_SHOW_TIME | (today ? 0 : DateUtils.FORMAT_SHOW_DATE | DateUtils.FORMAT_ABBREV_MONTH));
    }

    private String windowsPath(File file) {
        String prefix = prefixDir.getPath() + "/drive_c/";
        String path = file.getPath();
        if (path.startsWith(prefix)) return "C:\\" + path.substring(prefix.length()).replace('/', '\\');
        String root = rootDir.getPath() + "/";
        if (path.startsWith(root)) return "Z:\\" + path.substring(root.length()).replace('/', '\\');
        return "";
    }

    private void write() {
        StringBuilder out = new StringBuilder();
        try {
            row(out, "section", "problems.recent", activity.getString(R.string.s11_problem_recent), "", "");
            if (problems.isEmpty())
                row(out, "info", "problems.none", activity.getString(R.string.s11_problem_none), activity.getString(R.string.s11_problem_none_desc), "");
            for (int i = 0; i < problems.size(); i++) {
                Problem p = problems.get(i);
                row(out, "log", "problems.p" + i, p.title, p.detail + " · " + when(p.time), p.report != null ? windowsPath(p.report) : "");
            }

            ArrayList<UnityLog> logs = new ArrayList<>(unityLogs.values());
            logs.removeIf(log -> !log.file.isFile());
            logs.sort((a, b) -> Long.compare(b.modified, a.modified));
            if (!logs.isEmpty()) {
                row(out, "section", "problems.unity", activity.getString(R.string.s11_problem_unity), "", "");
                for (int i = 0; i < logs.size() && i < MAX_UNITY_LOGS; i++) {
                    UnityLog log = logs.get(i);
                    row(out, "log", "problems.u" + i, log.product, when(log.modified) + " · " + log.summary, windowsPath(log.file));
                }
            }

            row(out, "section", "problems.winesection", activity.getString(R.string.s11_problem_wine), "", "");
            File wineLog = new File(logsDir, WINE_LOG_NAME);
            if (wineLog.isFile() && !lines.isEmpty())
                row(out, "log", "problems.wine", activity.getString(R.string.s11_problem_wine_log),
                    activity.getString(R.string.s11_problem_wine_log_desc, lines.size()), windowsPath(wineLog));
            else
                row(out, "info", "problems.wine", activity.getString(R.string.s11_problem_wine_log), activity.getString(R.string.s11_problem_wine_log_none), "");
            row(out, "action", "problems.clear", activity.getString(R.string.s11_problem_clear),
                activity.getString(R.string.s11_problem_clear_desc), activity.getString(R.string.s11_problem_clear_button));
        }
        catch (RuntimeException e) {
            e.printStackTrace();
        }
        String rows = out.toString();
        if (rows.equals(written) && configFile.isFile()) return;
        written = rows;
        writeAtomically(configFile, "S11PROBLEMS\t1\t" + (++revision) + "\n" + rows);
    }

    private static void writeAtomically(File file, String text) {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory()) parent.mkdirs();
        File temp = new File(parent, file.getName() + ".tmp");
        try (FileOutputStream stream = new FileOutputStream(temp)) {
            stream.write(text.getBytes(StandardCharsets.UTF_8));
        }
        catch (IOException e) {
            return;
        }
        if (!temp.renameTo(file)) FileUtils.delete(temp);
    }
}
