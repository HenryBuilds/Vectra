;; Minimal Clojure fixture for language parse-validation tests.

(ns demo.core
  (:require [clojure.string :as str]))

(defn greet [label]
  (str "Hello, " label))

(defn add [a b]
  (+ a b))

(def default-label "world")
