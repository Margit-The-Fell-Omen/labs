package dev.ushki.controller;

import dev.ushki.model.LaterModel;
import dev.ushki.view.MainView;

public class Controller {

    private final LaterModel model;
    private final MainView view;

    public Controller(LaterModel model, MainView view) {
        this.model = model;
        this.view = view;
        this.view.setController(this);
        this.model.addListener(this.view);
    }

    public void onEnterData() {
        Integer age = view.askAge();
        if (age == null) {
            return;
        }
        if (age < 0 || age > 120) {
            view.showError("Возраст должен быть в диапазоне от 0 до 120 лет");
            return;
        }
        model.setAge(age);
    }
}
