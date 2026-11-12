package io.github.pellcorp.grumpyscreen;

import java.util.concurrent.Executor;
import java.util.function.BiPredicate;
import java.util.function.Consumer;

/** Checks one selection; replies for an old selection or a closed picker are ignored. */
final class PrinterConnectionCheck {
    enum State { UNCHECKED, CHECKING, ONLINE, OFFLINE }

    private final Executor background;
    private final Executor main;
    private final BiPredicate<String, Integer> probe;
    private final Consumer<State> listener;
    private int attempt;
    private State state = State.UNCHECKED;

    PrinterConnectionCheck(Executor background, Executor main,
                           BiPredicate<String, Integer> probe, Consumer<State> listener) {
        this.background = background;
        this.main = main;
        this.probe = probe;
        this.listener = listener;
    }

    // Call these methods on the UI thread, including while editing an address.
    void cancel() {
        attempt++;
        setState(State.UNCHECKED);
    }

    void check(String host, int port) {
        final int request = ++attempt;
        setState(State.CHECKING);
        background.execute(() -> {
            boolean online = probe.test(host, port);
            main.execute(() -> {
                if (request == attempt) {
                    setState(online ? State.ONLINE : State.OFFLINE);
                }
            });
        });
    }

    boolean canConnect() {
        return state == State.ONLINE;
    }

    private void setState(State state) {
        this.state = state;
        listener.accept(state);
    }
}
