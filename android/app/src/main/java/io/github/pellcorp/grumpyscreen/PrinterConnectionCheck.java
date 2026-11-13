package io.github.pellcorp.grumpyscreen;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.function.BiPredicate;

/** Checks listed printers independently without exposing pending network requests. */
final class PrinterConnectionCheck {
    enum State { UNCHECKED, ONLINE, OFFLINE }

    interface Listener {
        void onStateChanged(String host, int port, State state);
    }

    private static final class Target {
        final String host;
        final int port;
        boolean listed;
        int attempt;
        boolean checking;
        State state = State.UNCHECKED;

        Target(String host, int port) {
            this.host = host;
            this.port = port;
        }
    }

    private final Executor background;
    private final Executor main;
    private final BiPredicate<String, Integer> probe;
    private final Listener listener;
    private final Map<String, Target> targets = new LinkedHashMap<>();
    private Target selected;

    PrinterConnectionCheck(Executor background, Executor main,
                           BiPredicate<String, Integer> probe, Listener listener) {
        this.background = background;
        this.main = main;
        this.probe = probe;
        this.listener = listener;
    }

    // Call these methods on the UI thread, including while editing an address.
    void cancel() {
        targets.clear();
        selected = null;
        listener.onStateChanged(null, 0, State.UNCHECKED);
    }

    /** Include a saved printer in subsequent background checks. */
    void watch(String host, int port) {
        target(host, port).listed = true;
    }

    void remove(String host, int port) {
        Target target = targets.get(key(host, port));
        if (target != null) {
            target.listed = false;
            if (target != selected) {
                targets.remove(key(host, port));
            }
        }
    }

    void clearSelection() {
        if (selected != null && !selected.listed) {
            targets.remove(key(selected.host, selected.port));
        }
        selected = null;
        listener.onStateChanged(null, 0, State.UNCHECKED);
    }

    void select(String host, int port) {
        if (!isSelected(host, port)) {
            clearSelection();
            selected = target(host, port);
            listener.onStateChanged(host, port, selected.state);
        }
        if (selected.state == State.UNCHECKED) {
            check(selected);
        }
    }

    boolean isSelected(String host, int port) {
        return selected != null && selected.host.equals(host) && selected.port == port;
    }

    /** Network discovery is already a successful check; ignore older pending replies. */
    void found(String host, int port) {
        Target target = target(host, port);
        target.listed = true;
        target.attempt++;
        target.checking = false;
        setState(target, State.ONLINE);
    }

    void refresh() {
        for (Target target : new ArrayList<>(targets.values())) {
            check(target);
        }
    }

    private void check(Target target) {
        if (target.checking) {
            return;
        }
        target.checking = true;
        final int request = ++target.attempt;
        background.execute(() -> {
            boolean online = probe.test(target.host, target.port);
            main.execute(() -> {
                if (targets.get(key(target.host, target.port)) == target && request == target.attempt) {
                    target.checking = false;
                    setState(target, online ? State.ONLINE : State.OFFLINE);
                }
            });
        });
    }

    boolean canConnect() {
        return selected != null && selected.state == State.ONLINE;
    }

    private Target target(String host, int port) {
        return targets.computeIfAbsent(key(host, port), ignored -> new Target(host, port));
    }

    private static String key(String host, int port) {
        return host + ":" + port;
    }

    private void setState(Target target, State state) {
        if (target.state != state) {
            target.state = state;
            listener.onStateChanged(target.host, target.port, state);
        }
    }
}
