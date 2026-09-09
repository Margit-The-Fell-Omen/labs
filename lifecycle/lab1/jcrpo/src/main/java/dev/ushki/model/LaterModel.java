package dev.ushki.model;

import java.beans.PropertyChangeListener;
import java.beans.PropertyChangeSupport;

public class LaterModel {

    private static final int START_AGE = 5;
    private static final int TIMES_PER_DAY = 5;
    private static final int DAYS_PER_YEAR = 365;

    private int age = -1;
    private long timesSaidLater;

    private final PropertyChangeSupport support = new PropertyChangeSupport(this);

    public void addListener(PropertyChangeListener listener) {
        support.addPropertyChangeListener(listener);
    }

    public void removeListener(PropertyChangeListener listener) {
        support.removePropertyChangeListener(listener);
    }

    public int getAge() {
        return age;
    }

    public long getTimesSaidLater() {
        return timesSaidLater;
    }

    public void setAge(int newAge) {
        int oldAge = this.age;
        this.age = newAge;
        this.timesSaidLater = calculate(newAge);
        support.firePropertyChange("result", oldAge, newAge);
    }

    private long calculate(int age) {
        if (age <= START_AGE) {
            return 0;
        }
        return (long) (age - START_AGE) * DAYS_PER_YEAR * TIMES_PER_DAY;
    }
}
