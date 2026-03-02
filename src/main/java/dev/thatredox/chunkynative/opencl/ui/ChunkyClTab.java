package dev.thatredox.chunkynative.opencl.ui;

import dev.thatredox.chunkynative.opencl.context.ContextManager;
import dev.thatredox.chunkynative.opencl.context.KernelLoader;
import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.geometry.Insets;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.layout.VBox;
import javafx.util.Duration;
import se.llbit.chunky.renderer.scene.Scene;
import se.llbit.chunky.ui.render.RenderControlsTab;

import javafx.scene.control.Slider;
import javafx.scene.layout.HBox;

public class ChunkyClTab implements RenderControlsTab {
    protected final VBox box;
    private Scene scene;
    private final Label renderTimeLabel;
    private final Timeline renderTimeTicker;

    // 靜態變數供渲染器存取
    public static float russianRouletteThreshold = 50.0f;
    public static int virtualDepth = 10;

    public ChunkyClTab(Scene scene) {
        this.scene = scene;

        box = new VBox(10.0);
        box.setPadding(new Insets(10.0));

        renderTimeLabel = new Label();
        updateRenderTimeLabel();
        box.getChildren().add(renderTimeLabel);

        // Russian Roulette UI
        Label rrLabel = new Label("Russian Roulette Threshold: 50%");
        Slider rrSlider = new Slider(0, 100, 50);
        rrSlider.setShowTickLabels(true);
        rrSlider.valueProperty().addListener((obs, oldVal, newVal) -> {
            russianRouletteThreshold = newVal.floatValue();
            rrLabel.setText(String.format("Russian Roulette Threshold: %d%%", (int)russianRouletteThreshold));
            scene.softRefresh();
        });
        box.getChildren().addAll(rrLabel, rrSlider);

        // Virtual Depth UI
        Label vdLabel = new Label("Virtual Depth: 10 (1024 Blocks)");
        Slider vdSlider = new Slider(7, 16, 10);
        vdSlider.setMajorTickUnit(1);
        vdSlider.setMinorTickCount(0);
        vdSlider.setSnapToTicks(true);
        vdSlider.setShowTickLabels(true);
        vdSlider.valueProperty().addListener((obs, oldVal, newVal) -> {
            virtualDepth = newVal.intValue();
            int blocks = 1 << virtualDepth;
            vdLabel.setText(String.format("Virtual Depth: %d (%d Blocks)", virtualDepth, blocks));
            scene.softRefresh();
        });
        box.getChildren().addAll(vdLabel, vdSlider);

        Button deviceSelectorButton = new Button("Select OpenCL Device");
        deviceSelectorButton.setOnMouseClicked(event -> {
            DeviceSelector selector = new DeviceSelector();
            selector.show();
        });
        box.getChildren().add(deviceSelectorButton);

        if (KernelLoader.canHotReload()) {
            Button reloadButton = new Button("Reload!");
            reloadButton.setOnMouseClicked(event -> {
                ContextManager.reload();
                scene.refresh();
            });
            box.getChildren().add(reloadButton);
        }

        renderTimeTicker = new Timeline(
                new KeyFrame(Duration.millis(200), event -> updateRenderTimeLabel())
        );
        renderTimeTicker.setCycleCount(Timeline.INDEFINITE);
        renderTimeTicker.play();
    }

    @Override
    public void update(Scene scene) {
        this.scene = scene;
        updateRenderTimeLabel();
    }

    @Override
    public String getTabTitle() {
        return "OpenCL";
    }

    @Override
    public Node getTabContent() {
        return box;
    }

    private void updateRenderTimeLabel() {
        long millis = OpenClRenderTimer.getElapsedMillis();
        double seconds = millis / 1000.0;
        String suffix = OpenClRenderTimer.isRunning() ? " (running)" : "";
        renderTimeLabel.setText(String.format("Render Time: %.1f s%s", seconds, suffix));
    }
}
