library(ggplot2)

data_sensor <- c(0.085, 0.085, 0.311, 0.085)
data_led <- c(0.184, 0.177, 0.184, 0.177)


df <- data.frame(
  valor = c(data_sensor, data_led),
  grupo = factor(c(
    rep("Sensor", length(data_sensor)),
    rep("LED", length(data_led))
  ))
)

ggplot(df, aes(x = grupo, y = valor, fill = grupo)) +
  geom_boxplot() +
  labs(
    y = "Tempo em milisegundos (ms)"
  ) + theme_minimal() + scale_fill_discrete()

citation("ggplot2")
