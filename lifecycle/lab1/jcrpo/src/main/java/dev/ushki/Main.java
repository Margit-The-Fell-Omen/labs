package dev.ushki;

import dev.ushki.controller.Controller;
import dev.ushki.model.LaterModel;
import dev.ushki.view.MainView;

import javax.swing.SwingUtilities;

public class Main {

    static void main(String[] args) {
        LaterModel model = new LaterModel();
        MainView view = new MainView();
        new Controller(model, view);

        SwingUtilities.invokeLater(() -> view.setVisible(true));
    }
}
