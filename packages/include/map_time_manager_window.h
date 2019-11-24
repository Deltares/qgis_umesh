#ifndef dock_window_H
#define dock_window_H

#include <QObject>
#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDesktopWidget>
#include <QDockWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QToolBar>
#include <QVBoxLayout>

#include <qmath.h>
#include <qgsapplication.h>
#include <qgisplugin.h>

#include <direct.h> // for getcwd
#include <stdlib.h> // for MAX_PATH

#include "ugrid.h"
#include "MyDrawingCanvas.h"
#include "my_date_time_edit.h"
#include "QColorRampEditor.h"
#include "map_property_window.h"
#include "map_property.h"

class MapTimeManagerWindow
    : public QDockWidget

{
    Q_OBJECT

public:
    static int object_count;

    public:
        MapTimeManagerWindow(UGRID *, MyCanvas *);
        ~MapTimeManagerWindow();
        static int get_count();

    public slots:
        void closeEvent(QCloseEvent *);
        void button_group_pressed(int);
        void cb_clicked(int);

        void start_reverse();
        void pause_time_loop();
        void start_forward();

        void goto_begin();
        void one_step_backward();
        void one_step_forward();
        void goto_end();

        void first_date_time_changed(const QDateTime &);
        void last_date_time_changed(const QDateTime &);
        void curr_date_time_changed(const QDateTime &); 

    public slots:
        void MyMouseReleaseEvent(QMouseEvent* e);
        void ramp_changed();
        void show_hide_map_data();
        void contextMenu(const QPoint &);

    public:
        QDockWidget * map_panel;

    private:
        UGRID * _ugrid_file;
        MyCanvas * _MyCanvas;
        void create_window();
        QGridLayout * create_date_time_layout();
        QHBoxLayout * create_push_buttons_layout_animation();
        QHBoxLayout * create_push_buttons_layout_steps();
        QSlider *create_time_slider();
        QPushButton * show_parameter();
        QComboBox * create_parameter_selection();
        QColorRampEditor * create_color_ramp();
        QColorRampEditor * m_ramph;

        void setValue(int);
        void setSliderValue(QDateTime);

        QPushButton * pb_reverse;
        QPushButton * pb_start;
        QPushButton * pb_pauze;

        MyQDateTimeEdit * first_date_time;
        MyQDateTimeEdit * curr_date_time;
        MyQDateTimeEdit * last_date_time;

        int nr_times;
        QVector<QDateTime> _q_times;
        int _current_step;
        QSlider * m_slider;
        bool stop_time_loop;
        int first_date_time_indx;
        int last_date_time_indx;
        QComboBox * _cb;
        bool m_show_map_data;
};

#endif