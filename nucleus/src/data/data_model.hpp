#pragma once
#include "struct_model.hpp"

namespace data {
    /**
     * Utility classes and templates to simplify modeled data. This can be a mix-in with a
     * data::SharedStruct/like or can be stand-alone.
     */
    class Model {
    private:
        class Node {
            std::string_view _name; // Assume const string
            Node *_next{nullptr};

        public:
            explicit Node(const std::string_view name, Model *model) noexcept : _name(name) {
                if(model->_last == nullptr) {
                    model->_first = model->_last = this;
                } else {
                    model->_last->_next = this;
                    model->_last = this;
                }
            }
            virtual ~Node() noexcept = default;
            Node(const Node &) = delete;
            Node(Node &&) = delete;
            Node &operator=(const Node &) = delete;
            Node &operator=(Node &&) = delete;

            const Node *next() const {
                return _next;
            }
            Node *next() {
                return _next;
            }
            void read(const data::StructModelBase *dataSource) {
                StructElement el = dataSource->get(_name);
                set(el);
            }
            virtual void write(data::StructModelBase *dataTarget) const {
                StructElement el = getElement();
                dataTarget->put(_name, el);
            }
            virtual void set(const StructElement &el) = 0;
            [[nodiscard]] virtual StructElement getElement() const = 0;
        };
        Node *_first{nullptr};
        Node *_last{nullptr};

        void copy(const Model &other) {
            Node *dest = _first;
            const Node *src = other._first;
            while(dest != nullptr && src != nullptr) {
                dest->set(src->getElement());
                dest = dest->next();
                src = src->next();
            }
            assert(dest == nullptr && src == nullptr);
        }

    public:
        template<typename T>
        class Field : private Node {
            std::optional<T> _value;

        public:
            using Type = T;
            Field() = delete;
            Field(const Field &) = delete;
            Field(Field &&) = delete;
            Field &operator=(const Field &) = delete;
            Field &operator=(Field &&) = delete;
            ~Field() noexcept override = default;
            Field(std::string_view name, Model *model) noexcept : Node(name, model) {
            }
            void set(const StructElement &el) override;
            [[nodiscard]] StructElement getElement() const override;
        };

        Model() noexcept = default;
        Model(const Model &other) : Model() {
            copy(other);
        }
        Model &operator=(const Model &other) {
            copy(other);
            return *this;
        }
        Model(Model &&) = delete;
        Model &operator=(Model &&) = delete;
        ~Model() noexcept = default;

        /**
         * Fill fields from data structure.
         */
        void read(const std::shared_ptr<const data::StructModelBase> &dataSource) {
            read(dataSource.get());
        }

        /**
         * Fill fields from data structure.
         */
        void read(const data::StructModelBase *dataSource) {
            for(Node *node = _first; node != nullptr; node = node->next()) {
                node->read(dataSource);
            }
        }

        /**
         * Write fields into data structure.
         */
        void write(const std::shared_ptr<data::StructModelBase> &dataSource) const {
            write(dataSource.get());
        }

        /**
         * Fill fields from data structure.
         */
        void write(data::StructModelBase *dataSource) const {
            for(const Node *node = _first; node != nullptr; node = node->next()) {
                node->write(dataSource);
            }
        }
    };

    template<typename T>
    void Model::Field<T>::set(const data::StructElement &el) {
        _value = el.getAs<T>();
    }

    template<typename T>
    data::StructElement Model::Field<T>::getElement() const {
        return data::StructElement(_value);
    }

} // namespace data